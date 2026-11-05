#include "tt.h"
#include "search.h"
#include <algorithm>

TranspositionTable TT;

void TranspositionTable::resize(size_t mb) {
    size_t entries = 1;
    while (entries * 2 * sizeof(TTEntry) <= mb * 1024 * 1024) entries *= 2;
    table.assign(entries, TTEntry{});
    mask = entries - 1;
}

void TranspositionTable::clear() { std::fill(table.begin(), table.end(), TTEntry{}); }

const TTEntry* TranspositionTable::probe(uint64_t key) const {
    const TTEntry& e = table[key & mask];
    return e.key == key && e.bound != BOUND_NONE ? &e : nullptr;
}

// ponytail: one slot per index, replace unless the old entry for this same position was
// searched deeper. Buckets with aging are the upgrade when the table fills up in long games.
void TranspositionTable::store(uint64_t key, int depth, int score, Bound bound, Move move, int ply) {
    TTEntry& e = table[key & mask];
    if (e.key == key && e.depth > depth && bound != BOUND_EXACT) return;
    if (e.key != key || move) e.move = move;  // keep the old best move if we have none
    e.key = key;
    e.score = int16_t(score_to_tt(score, ply));
    e.depth = int8_t(depth);
    e.bound = bound;
}

int score_to_tt(int score, int ply) {
    if (score > MATE - MAX_PLY) return score + ply;
    if (score < -MATE + MAX_PLY) return score - ply;
    return score;
}

int score_from_tt(int score, int ply) {
    if (score > MATE - MAX_PLY) return score - ply;
    if (score < -MATE + MAX_PLY) return score + ply;
    return score;
}
