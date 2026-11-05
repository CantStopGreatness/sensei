// Milestone 0 + 1 checks: FEN round-trip, then perft against Chess Programming Wiki numbers.
// Usage: perft_tests [--deep]   (--deep adds one more ply per position; takes minutes)
#include "perft.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>

static int failures = 0;

static void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << '\n';
    if (!ok) failures++;
}

static void fen_tests() {
    std::cout << "FEN round-trip\n";
    const char* fens[] = {
        START_FEN,
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
        "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "8/8/8/8/8/8/8/K6k w - - 99 150",
        "4k3/8/8/8/8/8/8/4K2R w K - 0 1",
        "r3k3/8/8/8/8/8/8/4K3 b q - 12 40",
        "8/P7/8/8/8/8/7p/K6k b - - 0 60",
    };
    for (const char* fen : fens) {
        Position pos;
        check(pos.set_fen(fen) && pos.fen() == fen, fen);
    }

    // Missing counters default to "0 1".
    Position pos;
    check(pos.set_fen("8/8/8/8/8/8/8/K6k w - -") && pos.fen() == "8/8/8/8/8/8/8/K6k w - - 0 1",
          "4-field FEN gets default counters");

    const char* bad[] = {
        "",
        "8/8/8/8/8/8/8/8 w - - 0 1",                                   // no kings
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN w KQkq - 0 1",     // short rank
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1",    // bad side
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkx - 0 1",    // bad castling
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e4 0 1",   // bad ep rank
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - x 1",    // bad counter
        "4k3/8/8/8/8/8/8/4K3 w K - 0 1",                               // castling w/o rook
    };
    for (int i = 0; i < 8; i++) check(!pos.set_fen(bad[i]), std::string("rejects \"") + bad[i] + '"');
    check(!pos.set_fen("4k3/8/8/8/8/8/8/4R1K1 w - - 0 1"), "rejects side-not-to-move in check");
}

struct PerftCase {
    const char* name;
    const char* fen;
    std::vector<uint64_t> counts;  // depth 1, 2, ...
};

static void perft_tests(bool deep) {
    // From https://www.chessprogramming.org/Perft_Results
    const std::vector<PerftCase> cases = {
        {"Start position", START_FEN, {20, 400, 8902, 197281, 4865609, 119060324}},
        {"Kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
         {48, 2039, 97862, 4085603, 193690690}},
        {"Position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
         {14, 191, 2812, 43238, 674624, 11030083}},
        {"Position 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
         {6, 264, 9467, 422333, 15833292}},
        {"Position 4 mirrored", "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1",
         {6, 264, 9467, 422333, 15833292}},
        {"Position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
         {44, 1486, 62379, 2103487, 89941194}},
        {"Position 6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
         {46, 2079, 89890, 3894594, 164075551}},
    };
    std::cout << "\nPerft\n";
    for (const auto& c : cases) {
        Position pos;
        pos.set_fen(c.fen);
        // Default runs every depth but the last listed; --deep runs all of them.
        size_t max_depth = c.counts.size() - (deep ? 0 : 1);
        for (size_t d = 1; d <= max_depth; d++) {
            auto t0 = std::chrono::steady_clock::now();
            uint64_t n = perft(pos, int(d));
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            check(n == c.counts[d - 1], std::string(c.name) + " depth " + std::to_string(d) + ": " +
                                            std::to_string(n) + " (expected " + std::to_string(c.counts[d - 1]) +
                                            ", " + std::to_string(int(ms)) + " ms)");
        }
        check(pos.fen() == c.fen, std::string(c.name) + " restored after make/unmake");
    }
}

int main(int argc, char** argv) {
    init_bitboards();
    bool deep = argc > 1 && std::strcmp(argv[1], "--deep") == 0;
    fen_tests();
    perft_tests(deep);
    std::cout << '\n' << (failures ? std::to_string(failures) + " FAILED" : "All tests passed") << '\n';
    return failures ? 1 : 0;
}
