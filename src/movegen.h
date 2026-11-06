#pragma once
#include "position.h"

struct MoveList {
    Move moves[256];  // no legal chess position has more than 218 moves
    int size = 0;
    void add(Move m) { moves[size++] = m; }
    const Move* begin() const { return moves; }
    const Move* end() const { return moves + size; }
};

// Every move that obeys piece movement; may leave own king in check. With captures_only, just
// captures (en passant included) and promotions, for quiescence search.
void generate_pseudo_legal(const Position& pos, MoveList& list, bool captures_only = false);
// Only moves that don't leave own king in check. Position is restored on return.
void generate_legal(Position& pos, MoveList& list, bool captures_only = false);
