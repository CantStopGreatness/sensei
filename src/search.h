#pragma once
#include "position.h"
#include <atomic>
#include <cstdint>
#include <ostream>

constexpr int MAX_PLY = 64;
constexpr int MATE = 32000;  // mate in N plies scores MATE - N

struct SearchLimits {
    int depth = MAX_PLY - 1;
    int64_t time_ms = -1;  // hard limit for this move: abort the search here; -1 = no limit
    int64_t soft_ms = -1;  // target time: don't start a depth that likely can't finish by then
    // While *pondering is true the time limits are ignored; when it turns false (UCI "ponderhit")
    // the clock starts and the limits apply from that moment. nullptr = not a ponder search.
    const std::atomic<bool>* pondering = nullptr;
    int threads = 1;  // search threads, main one included (UCI option Threads)
    // Strength limit (UCI_Elo, 400..2400); 0 = full strength. Caps the depth and picks among root
    // moves at random, weighted by score, so lower levels play weaker moves more often.
    int elo = 0;
    uint32_t seed = 0;  // random seed for the weighted pick; 0 = seed from std::random_device
};

struct SearchResult {
    Move best = 0;  // 0 when there are no legal moves
    int score = 0;
    int depth = 0;
    uint64_t nodes = 0;
    Move ponder = 0;  // expected reply, second move of the PV; 0 if unknown
};

// Static exchange evaluation of a capture, in centipawns for the side making it: material won
// or lost on the target square if both sides keep recapturing with their least valuable piece.
// Promotions are treated as plain captures.
int see(const Position& pos, Move m);

// Iterative deepening alpha-beta. Prints UCI "info" lines to `out` after each depth.
// Returns early when `stop` becomes true or time runs out; depth 1 always completes.
SearchResult search(Position& pos, const SearchLimits& limits, const std::atomic<bool>& stop, std::ostream& out);
