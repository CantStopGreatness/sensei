#pragma once
#include "types.h"
#include <atomic>
#include <cstddef>
#include <memory>

enum Bound : uint8_t { BOUND_NONE, BOUND_UPPER, BOUND_LOWER, BOUND_EXACT };

struct TTEntry {
    Move move = 0;
    int16_t score = 0;
    int8_t depth = 0;
    uint8_t bound = BOUND_NONE;
};

// Fixed-size hash table of search results, indexed by Zobrist key, shared by all search threads.
// Lockless: each slot holds the packed entry and key ^ entry as two relaxed atomics, so a slot
// torn by two threads writing at once fails the key check instead of returning mixed data.
class TranspositionTable {
public:
    explicit TranspositionTable(size_t mb = 16) { resize(mb); }
    void resize(size_t mb);  // rounds down to a power-of-two entry count; clears
    void clear();
    // True and fills `out` if this key is stored. Mate scores come back relative to the node
    // they were stored at; convert with score_from_tt.
    bool probe(uint64_t key, TTEntry& out) const;
    void store(uint64_t key, int depth, int score, Bound bound, Move move, int ply);

private:
    struct Slot {
        std::atomic<uint64_t> check{0};  // key ^ data
        std::atomic<uint64_t> data{0};
    };
    std::unique_ptr<Slot[]> table;
    size_t mask = 0;
};

extern TranspositionTable TT;  // one search at a time; its threads share this table

// Mate scores are stored as "mate in N from this node", not from the root, so the
// same entry is right whatever ply it is found at.
int score_to_tt(int score, int ply);
int score_from_tt(int score, int ply);
