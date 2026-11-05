#include "perft.h"
#include <chrono>
#include <iostream>
#include <string>

static int usage() {
    std::cerr << "usage:\n"
                 "  engine print [fen]\n"
                 "  engine perft <depth> [fen]\n"
                 "  engine divide <depth> [fen]\n"
                 "fen defaults to the start position\n";
    return 1;
}

int main(int argc, char** argv) {
    init_bitboards();
    if (argc < 2) return usage();
    std::string cmd = argv[1];
    bool has_depth = cmd == "perft" || cmd == "divide";
    if (!has_depth && cmd != "print") return usage();
    if (has_depth && argc < 3) return usage();

    int depth = 0;
    if (has_depth) {
        try { depth = std::stoi(argv[2]); } catch (...) { return usage(); }
        if (depth < 1 || depth > 15) return usage();
    }

    std::string fen;
    for (int i = has_depth ? 3 : 2; i < argc; i++) fen += std::string(argv[i]) + ' ';
    Position pos;
    if (!pos.set_fen(fen.empty() ? START_FEN : fen)) {
        std::cerr << "invalid FEN: " << fen << '\n';
        return 1;
    }

    if (cmd == "print") {
        std::cout << pos.pretty();
        return 0;
    }

    auto t0 = std::chrono::steady_clock::now();
    uint64_t nodes = cmd == "divide" ? divide(pos, depth, std::cout) : perft(pos, depth);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (cmd == "perft") std::cout << "Nodes: " << nodes << '\n';
    std::cout << "Time: " << secs << "s  NPS: " << uint64_t(nodes / (secs > 0 ? secs : 1e-9)) << '\n';
    return 0;
}
