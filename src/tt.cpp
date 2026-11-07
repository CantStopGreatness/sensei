#include "tt.h"
#include "search.h"

TranspositionTable TT;

// Packed entry: move (16 bits), score (16), depth (8), bound (8), generation (8).
static uint64_t pack(const TTEntry& e, uint8_t gen) {
    return uint64_t(e.move) | uint64_t(uint16_t(e.score)) << 16 | uint64_t(uint8_t(e.depth)) << 32 |
           uint64_t(e.bound) << 40 | uint64_t(gen) << 48;
}

static TTEntry unpack(uint64_t d) {
    return {Move(d & 0xFFFF), int16_t(uint16_t(d >> 16)), int8_t(uint8_t(d >> 32)), uint8_t(d >> 40)};
}

void TranspositionTable::resize(size_t mb) {
    size_t buckets = 1;
    while (buckets * 2 * sizeof(Bucket) <= mb * 1024 * 1024) buckets *= 2;
    table = std::make_unique<Bucket[]>(buckets);
    mask = buckets - 1;
}

void TranspositionTable::clear() {
    for (size_t i = 0; i <= mask; i++)
        for (Slot& s : table[i].slots) s.check = 0, s.data = 0;
    generation = 0;
}

bool TranspositionTable::probe(uint64_t key, TTEntry& out) const {
    for (const Slot& s : table[key & mask].slots) {
        uint64_t data = s.data.load(std::memory_order_relaxed);
        if ((s.check.load(std::memory_order_relaxed) ^ data) != key) continue;
        out = unpack(data);
        if (out.bound != BOUND_NONE) return true;
    }
    return false;
}

void TranspositionTable::store(uint64_t key, int depth, int score, Bound bound, Move move, int ply) {
    Slot* target = nullptr;
    int worst = 1 << 30;
    TTEntry old;
    bool same = false;
    for (Slot& s : table[key & mask].slots) {
        uint64_t data = s.data.load(std::memory_order_relaxed);
        if ((s.check.load(std::memory_order_relaxed) ^ data) == key) {  // this position: update in place
            target = &s, old = unpack(data), same = old.bound != BOUND_NONE;
            break;
        }
        // Otherwise evict the least valuable: shallow, or left over from earlier searches.
        int age = uint8_t(generation - uint8_t(data >> 48));
        int value = unpack(data).bound == BOUND_NONE ? -(1 << 20) : int8_t(uint8_t(data >> 32)) - 8 * age;
        if (value < worst) worst = value, target = &s;
    }
    if (same && old.depth > depth && bound != BOUND_EXACT) return;
    TTEntry e{same && !move ? old.move : move, int16_t(score_to_tt(score, ply)), int8_t(depth), uint8_t(bound)};
    uint64_t data = pack(e, generation);
    target->data.store(data, std::memory_order_relaxed);
    target->check.store(key ^ data, std::memory_order_relaxed);
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
