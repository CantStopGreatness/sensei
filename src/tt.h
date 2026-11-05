#pragma once
#include "types.h"
#include <cstddef>
#include <vector>

enum Bound : uint8_t { BOUND_NONE, BOUND_UPPER, BOUND_LOWER, BOUND_EXACT };

struct TTEntry {
    uint64_t key = 0;
    Move move = 0;
    int16_t score = 0;
    int8_t depth = 0;
    uint8_t bound = BOUND_NONE;
};

// Fixed-size hash table of search results, indexed by Zobrist key.
class TranspositionTable {
public:
    explicit TranspositionTable(size_t mb = 16) { resize(mb); }
    void resize(size_t mb);  // rounds down to a power-of-two entry count; clears
    void clear();
    // Entry for this key, or nullptr. Mate scores come back relative to `ply`.
    const TTEntry* probe(uint64_t key) const;
    void store(uint64_t key, int depth, int score, Bound bound, Move move, int ply);

private:
    std::vector<TTEntry> table;
    size_t mask = 0;
};

extern TranspositionTable TT;  // one search runs at a time, so one shared table

// Mate scores are stored as "mate in N from this node", not from the root, so the
// same entry is right whatever ply it is found at.
int score_to_tt(int score, int ply);
int score_from_tt(int score, int ply);
