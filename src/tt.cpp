#include "tt.h"
#include "search.h"

TranspositionTable TT;

static uint64_t pack(const TTEntry& e) {
    return uint64_t(e.move) | uint64_t(uint16_t(e.score)) << 16 | uint64_t(uint8_t(e.depth)) << 32 |
           uint64_t(e.bound) << 40;
}

static TTEntry unpack(uint64_t d) {
    return {Move(d & 0xFFFF), int16_t(uint16_t(d >> 16)), int8_t(uint8_t(d >> 32)), uint8_t(d >> 40)};
}

void TranspositionTable::resize(size_t mb) {
    size_t entries = 1;
    while (entries * 2 * sizeof(Slot) <= mb * 1024 * 1024) entries *= 2;
    table = std::make_unique<Slot[]>(entries);
    mask = entries - 1;
}

void TranspositionTable::clear() {
    for (size_t i = 0; i <= mask; i++) table[i].check = 0, table[i].data = 0;
}

bool TranspositionTable::probe(uint64_t key, TTEntry& out) const {
    const Slot& s = table[key & mask];
    uint64_t data = s.data.load(std::memory_order_relaxed);
    if ((s.check.load(std::memory_order_relaxed) ^ data) != key) return false;
    out = unpack(data);
    return out.bound != BOUND_NONE;
}

// ponytail: one slot per index, replace unless the old entry for this same position was
// searched deeper. Buckets with aging are the upgrade when the table fills up in long games.
void TranspositionTable::store(uint64_t key, int depth, int score, Bound bound, Move move, int ply) {
    Slot& s = table[key & mask];
    TTEntry old;
    bool same = probe(key, old);
    if (same && old.depth > depth && bound != BOUND_EXACT) return;
    TTEntry e{same && !move ? old.move : move, int16_t(score_to_tt(score, ply)), int8_t(depth), uint8_t(bound)};
    uint64_t data = pack(e);
    s.data.store(data, std::memory_order_relaxed);
    s.check.store(key ^ data, std::memory_order_relaxed);
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
