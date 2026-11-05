// Milestone 3 checks: hashing, repetition, mates, not hanging pieces, search speed.
// Mate puzzles and their solution sets were verified independently with python-chess.
#include "movegen.h"
#include "search.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <vector>

static int failures = 0;

static void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << '\n';
    if (!ok) failures++;
}

static Position from_fen(const std::string& fen) {
    Position pos;
    if (!pos.set_fen(fen)) std::cout << "  bad test FEN " << fen << '\n';
    return pos;
}

static void play(Position& pos, const std::vector<std::string>& moves) {
    for (const auto& s : moves) {
        MoveList list;
        generate_legal(pos, list);
        auto it = std::find_if(list.begin(), list.end(), [&](Move m) { return move_to_uci(m) == s; });
        Undo u;
        pos.make_move(*it, u);
    }
}

// Incremental hash must equal a from-scratch hash at every node.
static bool hash_walk(Position& pos, int depth) {
    if (pos.hash != pos.compute_hash()) return false;
    if (depth == 0) return true;
    MoveList list;
    generate_legal(pos, list);
    for (Move m : list) {
        Undo u;
        pos.make_move(m, u);
        bool ok = hash_walk(pos, depth - 1);
        pos.unmake_move(m, u);
        if (!ok) return false;
    }
    return pos.hash == pos.compute_hash();
}

static SearchResult run(Position& pos, int depth) {
    std::atomic<bool> stop{false};
    std::ostringstream sink;
    SearchLimits limits;
    limits.depth = depth;
    return search(pos, limits, stop, sink);
}

int main() {
    init_bitboards();

    std::cout << "Hashing and repetition\n";
    for (const char* fen : {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                            "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"}) {
        Position pos = from_fen(fen);
        check(hash_walk(pos, 3), std::string("incremental hash matches at every node: ") + fen);
    }
    {
        Position pos = from_fen(START_FEN);
        uint64_t start = pos.hash;
        play(pos, {"g1f3", "g8f6", "f3g1"});
        check(!pos.is_repetition(), "no repetition before the cycle closes");
        play(pos, {"f6g8"});
        check(pos.is_repetition() && pos.hash == start, "knight shuffle repeats the start position");
    }

    std::cout << "\nMates\n";
    struct Puzzle { const char* fen; int moves; std::vector<std::string> solutions; };
    const std::vector<Puzzle> mates = {
        {"6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", 1, {"d1d8"}},
        {"r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4", 1, {"h5f7"}},
        {"6rk/6pp/8/6N1/8/8/8/6QK w - - 0 1", 1, {"g5f7"}},
        {"kbK5/pp6/1P6/8/8/8/8/R7 w - - 0 1", 2, {"a1a6"}},
        {"r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2bK2R w KQkq - 1 10", 2, {"d5f6"}},
        {"6k1/pp4p1/2p5/2bp4/8/P5Pb/1P3rrP/2BRRN1K b - - 0 1", 2, {"g2g1"}},
        {"r1b2k1r/ppp1bppp/8/1B1Q4/5q2/2P5/PPP2PPP/R3R1K1 w - - 1 1", 2, {"d5d8"}},
        {"2k5/8/1K6/8/8/8/8/7R w - - 0 1", 2, {"h1d1"}},
    };
    for (const auto& p : mates) {
        Position pos = from_fen(p.fen);
        SearchResult r = run(pos, 2 * p.moves);
        std::string best = move_to_uci(r.best);
        bool found = std::find(p.solutions.begin(), p.solutions.end(), best) != p.solutions.end();
        check(found && r.score == MATE - (2 * p.moves - 1),
              "mate in " + std::to_string(p.moves) + ": played " + best + ", " + p.fen);
    }
    {
        // Second search with a warm TT used to stop at depth 1 on a stored mate in 4 (Qc5+).
        Position pos = from_fen("8/k7/2K5/8/8/8/2Q5/8 w - - 0 1");
        run(pos, 10);
        SearchResult r = run(pos, 10);
        check(r.score == MATE - 3, "warm TT still finds the shortest mate (mate in 2): played " + move_to_uci(r.best));
    }
    {
        Position pos = from_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        check(run(pos, 3).best == 0, "stalemate: no move, no crash");
    }

    std::cout << "\nMaterial\n";
    struct Tactic { const char* fen; std::vector<std::string> good; const char* what; };
    const std::vector<Tactic> tactics = {
        {"rnb1kbnr/pppp1ppp/8/4p3/4P2q/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3", {"f3h4"}, "takes a hanging queen"},
        {"rnbqkbnr/ppp2ppp/8/4p3/3pP3/2N5/PPPP1PPP/R1BQKBNR w KQkq - 0 4",
         {"c3b5", "c3d5", "c3e2", "c3b1", "c3a4"}, "moves a knight attacked by a pawn"},
        {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5Q2/PPPP1PPP/RNB1KBNR w KQkq - 2 3", {}, "doesn't play Qxf7+ into Kxf7"},
    };
    for (const auto& t : tactics) {
        Position pos = from_fen(t.fen);
        std::string best = move_to_uci(run(pos, 5).best);
        bool ok = t.good.empty() ? best != "f3f7" : std::find(t.good.begin(), t.good.end(), best) != t.good.end();
        check(ok, std::string(t.what) + ": played " + best);
    }

    std::cout << "\nSpeed\n";
    {
        Position pos = from_fen(START_FEN);
        auto t0 = std::chrono::steady_clock::now();
        SearchResult r = run(pos, 6);
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(r.depth == 6 && s < 3.0, "start position depth 6 in " + std::to_string(s) + "s, " +
                                           std::to_string(r.nodes) + " nodes");
    }

    std::cout << '\n' << (failures ? std::to_string(failures) + " FAILED" : "All tests passed") << '\n';
    return failures ? 1 : 0;
}
