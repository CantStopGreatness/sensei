#pragma once
#include "types.h"

constexpr Bitboard sq_bb(int sq) { return 1ULL << sq; }
constexpr Bitboard RANK_1 = 0xFFULL;
constexpr Bitboard RANK_8 = RANK_1 << 56;

inline int lsb(Bitboard b) { return __builtin_ctzll(b); }
inline int popcount(Bitboard b) { return __builtin_popcountll(b); }
inline int pop_lsb(Bitboard& b) { int s = lsb(b); b &= b - 1; return s; }

extern Bitboard KNIGHT_ATTACKS[64];
extern Bitboard KING_ATTACKS[64];
extern Bitboard PAWN_ATTACKS[2][64];  // [color][square]: squares a pawn of that color attacks

void init_bitboards();  // call once at startup
Bitboard bishop_attacks(int sq, Bitboard occupied);
Bitboard rook_attacks(int sq, Bitboard occupied);
