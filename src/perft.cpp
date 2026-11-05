#include "perft.h"
#include "movegen.h"

uint64_t perft(Position& pos, int depth) {
    if (depth == 0) return 1;
    MoveList list;
    generate_legal(pos, list);
    if (depth == 1) return list.size;  // bulk count: leaves needn't be made
    uint64_t nodes = 0;
    for (Move m : list) {
        Undo u;
        pos.make_move(m, u);
        nodes += perft(pos, depth - 1);
        pos.unmake_move(m, u);
    }
    return nodes;
}

uint64_t divide(Position& pos, int depth, std::ostream& out) {
    MoveList list;
    generate_legal(pos, list);
    uint64_t total = 0;
    for (Move m : list) {
        Undo u;
        pos.make_move(m, u);
        uint64_t n = depth > 1 ? perft(pos, depth - 1) : 1;
        pos.unmake_move(m, u);
        out << move_to_uci(m) << ": " << n << '\n';
        total += n;
    }
    out << "\nMoves: " << list.size << "\nNodes: " << total << '\n';
    return total;
}
