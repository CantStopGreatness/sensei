#pragma once
#include "position.h"
#include <cstdint>
#include <ostream>

// Number of leaf nodes at exactly `depth` plies.
uint64_t perft(Position& pos, int depth);
// Perft split by root move ("e2e4: 13134"), then the total. Returns the total.
uint64_t divide(Position& pos, int depth, std::ostream& out);
