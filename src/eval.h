#pragma once
#include "position.h"

constexpr int PIECE_VALUE[6] = {100, 320, 330, 500, 900, 0};

// Static evaluation in centipawns, from the side to move's point of view.
int evaluate(const Position& pos);
