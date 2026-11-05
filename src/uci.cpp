#include "uci.h"
#include "movegen.h"
#include <random>
#include <sstream>
#include <string>

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
        out << "info string bad position command" << std::endl;
        return;
    }

    Position p;
    if (!p.set_fen(fen)) {
        out << "info string invalid fen: " << fen << std::endl;
        return;
    }
    while (ss >> token) {
        Move m = parse_move(p, token);
        if (!m) {
            out << "info string illegal move: " << token << std::endl;
            return;
        }
        Undo u;
        p.make_move(m, u);
    }
    pos = p;
}

void uci_loop(std::istream& in, std::ostream& out) {
    Position pos;
    pos.set_fen(START_FEN);
    std::mt19937 rng(std::random_device{}());

    for (std::string line; std::getline(in, line);) {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "uci") {
            out << "id name Sensei\nid author Aryan Thakur\nuciok" << std::endl;
        } else if (cmd == "isready") {
            out << "readyok" << std::endl;
        } else if (cmd == "ucinewgame") {
            pos.set_fen(START_FEN);
        } else if (cmd == "position") {
            set_position(pos, ss, out);
        } else if (cmd == "go") {
            // v0.1 random mover: go's depth/movetime/clock arguments are ignored.
            // ponytail: answers instantly, so "stop" has nothing to interrupt; search
            // (Milestone 3) needs a search thread for stop to mean anything.
            MoveList list;
            generate_legal(pos, list);
            Move best = list.size ? list.moves[rng() % list.size] : 0;
            out << "bestmove " << (best ? move_to_uci(best) : "0000") << std::endl;
        } else if (cmd == "d") {
            out << pos.pretty() << std::flush;  // debug helper, not part of UCI
        } else if (cmd == "quit") {
            break;
        }
        // "stop" and unknown commands are ignored, as UCI requires.
    }
}
