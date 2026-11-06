#pragma once
#include "position.h"

// Piece values for search (SEE, move ordering). The evaluation has its own, tunable ones.
constexpr int PIECE_VALUE[6] = {100, 320, 330, 500, 900, 0};

// Every number the evaluation uses, in middlegame/endgame pairs where it has both. All ints,
// so the tuner can treat it as one flat array (see eval_params()).
struct EvalParams {
    int piece_mg[6], piece_eg[6];
    int pst_mg[6][64], pst_eg[6][64];  // laid out as seen from White: first row is rank 8
    int passed_mg[8], passed_eg[8];    // by relative rank
    int doubled_mg, doubled_eg;
    int isolated_mg, isolated_eg;
    int bishop_pair_mg, bishop_pair_eg;
    int rook_open_mg, rook_open_eg;
    int rook_semi_open_mg, rook_semi_open_eg;
    int mobility_mg[6], mobility_eg[6];  // per reachable square beyond MOBILITY_BASE
    int attack_units[6];                 // weight per piece type attacking the enemy king zone
    int attack_scale, attack_cap;        // penalty = min(attack_scale * units^2, attack_cap)
    int shield_near, shield_far;         // own pawns one and two ranks in front of the king
};

extern EvalParams EVAL;  // the live weights; the tuner edits them in place

constexpr int EVAL_PARAM_COUNT = sizeof(EvalParams) / sizeof(int);
inline int* eval_params() { return reinterpret_cast<int*>(&EVAL); }

// Static evaluation in centipawns, from the side to move's point of view.
int evaluate(const Position& pos);
