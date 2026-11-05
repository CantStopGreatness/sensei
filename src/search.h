#pragma once
#include "position.h"
#include <atomic>
#include <cstdint>
#include <ostream>

constexpr int MAX_PLY = 64;
constexpr int MATE = 32000;  // mate in N plies scores MATE - N

struct SearchLimits {
    int depth = MAX_PLY - 1;
    int64_t time_ms = -1;  // hard limit for this move; -1 = no limit
};

struct SearchResult {
    Move best = 0;  // 0 when there are no legal moves
    int score = 0;
    int depth = 0;
    uint64_t nodes = 0;
};

// Iterative deepening alpha-beta. Prints UCI "info" lines to `out` after each depth.
// Returns early when `stop` becomes true or time runs out; depth 1 always completes.
SearchResult search(Position& pos, const SearchLimits& limits, const std::atomic<bool>& stop, std::ostream& out);
