#include "uci.h"
#include "movegen.h"
#include "search.h"
#include "tt.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

void say(std::ostream& out, const std::string& line) {
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    out << line;
    if (line.empty() || line.back() != '\n') out << '\n';
    out << std::flush;
}

static Move parse_move(Position& pos, const std::string& s) {
    MoveList list;
    generate_legal(pos, list);
    for (Move m : list)
        if (move_to_uci(m) == s) return m;
    return 0;  // 0 (a1a1) is never a legal move
}

// "position startpos|fen <fen> [moves m1 m2 ...]". Leaves pos unchanged on bad input.
static void set_position(Position& pos, std::istringstream& ss, std::ostream& out) {
    std::string token, fen;
    ss >> token;
    if (token == "startpos") {
        fen = START_FEN;
        ss >> token;  // "moves" or nothing
    } else if (token == "fen") {
        while (ss >> token && token != "moves") fen += token + ' ';
    } else {
        say(out, "info string bad position command");
        return;
    }

    Position p;
    if (!p.set_fen(fen)) {
        say(out, "info string invalid fen: " + fen);
        return;
    }
    while (ss >> token) {
        Move m = parse_move(p, token);
        if (!m) {
            say(out, "info string illegal move: " + token);
            return;
        }
        Undo u;
        p.make_move(m, u);
    }
    pos = p;
}

// Turns "go" arguments into search limits. With a clock, the target (soft) time is
// remaining / 25 + 3/4 of the increment; the search may run to 4x that (hard), but never
// past a third of the remaining time.
static SearchLimits parse_go(std::istringstream& ss, Color side, bool& infinite, bool& ponder) {
    SearchLimits limits;
    int64_t time[2] = {-1, -1}, inc[2] = {0, 0}, movetime = -1;
    int movestogo = 0;
    infinite = ponder = false;
    for (std::string t; ss >> t;) {
        if (t == "infinite") infinite = true;
        else if (t == "ponder") ponder = true;
        else if (t == "depth") ss >> limits.depth;
        else if (t == "movetime") ss >> movetime;
        else if (t == "wtime") ss >> time[WHITE];
        else if (t == "btime") ss >> time[BLACK];
        else if (t == "winc") ss >> inc[WHITE];
        else if (t == "binc") ss >> inc[BLACK];
        else if (t == "movestogo") ss >> movestogo;
    }
    limits.depth = std::clamp(limits.depth, 1, MAX_PLY - 1);
    if (infinite) return limits;
    if (movetime >= 0) {
        limits.time_ms = movetime;
    } else if (time[side] >= 0) {
        int64_t left = std::max<int64_t>(1, time[side] - 50);  // keep 50 ms for process lag
        int divisor = movestogo > 0 ? std::min(movestogo, 25) : 25;
        int64_t soft = left / divisor + inc[side] * 3 / 4;
        limits.time_ms = std::max<int64_t>(1, std::min(soft * 4, left / 3));
        limits.soft_ms = std::min(soft, limits.time_ms);
    }
    return limits;
}

void uci_loop(std::istream& in, std::ostream& out) {
    Position pos;
    pos.set_fen(START_FEN);
    std::atomic<bool> stop{false};
    std::atomic<bool> pondering{false};  // between "go ponder" and "ponderhit"
    int threads = 1;
    bool limit_strength = false;
    int elo = 1200;
    std::thread searcher;
    auto finish_search = [&] {
        stop = true;
        if (searcher.joinable()) searcher.join();
    };

    for (std::string line; std::getline(in, line);) {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "uci") {
            say(out, "id name Sensei\nid author Aryan Thakur\n"
                     "option name Hash type spin default 16 min 1 max 1024\n"
                     "option name Ponder type check default false\n"
                     "option name Threads type spin default 1 min 1 max 64\n"
                     "option name UCI_LimitStrength type check default false\n"
                     "option name UCI_Elo type spin default 1200 min 400 max 2400\nuciok");
        } else if (cmd == "isready") {
            say(out, "readyok");
        } else if (cmd == "ucinewgame") {
            finish_search();
            pos.set_fen(START_FEN);
            TT.clear();
        } else if (cmd == "setoption") {
            // "setoption name Hash|Threads|UCI_LimitStrength|UCI_Elo value <v>"; others are ignored.
            finish_search();
            std::string name_kw, name, value_kw, value;
            if (ss >> name_kw >> name >> value_kw >> value) {
                long n = std::strtol(value.c_str(), nullptr, 10);
                if (name == "Hash" && n >= 1 && n <= 1024) TT.resize(size_t(n));
                if (name == "Threads" && n >= 1 && n <= 64) threads = int(n);
                if (name == "UCI_LimitStrength") limit_strength = value == "true";
                if (name == "UCI_Elo" && n > 0) elo = int(std::clamp(n, 400L, 2400L));
            }
        } else if (cmd == "position") {
            finish_search();
            set_position(pos, ss, out);
        } else if (cmd == "go") {
            finish_search();
            bool infinite, ponder;
            SearchLimits limits = parse_go(ss, pos.side, infinite, ponder);
            stop = false;
            pondering = ponder;
            if (ponder) limits.pondering = &pondering;
            limits.threads = threads;
            if (limit_strength) limits.elo = elo;
            searcher = std::thread([&out, &stop, &pondering, limits, infinite, p = pos]() mutable {
                SearchResult r = search(p, limits, stop, out);
                // UCI: after "go infinite", and while pondering, bestmove waits for "stop" or "ponderhit".
                while ((infinite || pondering) && !stop) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                std::string line = "bestmove " + (r.best ? move_to_uci(r.best) : "0000");
                if (r.best && r.ponder) line += " ponder " + move_to_uci(r.ponder);
                say(out, line);
            });
        } else if (cmd == "ponderhit") {
            pondering = false;  // the search keeps going, now on the clock
        } else if (cmd == "stop") {
            finish_search();
        } else if (cmd == "d") {
            say(out, pos.pretty());  // debug helper, not part of UCI
        } else if (cmd == "quit") {
            break;
        }
        // Unknown commands are ignored, as UCI requires.
    }
    finish_search();
}
