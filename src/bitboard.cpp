#include "bitboard.h"

Bitboard KNIGHT_ATTACKS[64];
Bitboard KING_ATTACKS[64];
Bitboard PAWN_ATTACKS[2][64];

static bool on_board(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }

static Bitboard step_attacks(int sq, const int (*dirs)[2], int n) {
    Bitboard b = 0;
    for (int i = 0; i < n; i++) {
        int f = file_of(sq) + dirs[i][0], r = rank_of(sq) + dirs[i][1];
        if (on_board(f, r)) b |= sq_bb(r * 8 + f);
    }
    return b;
}

void init_bitboards() {
    static const int knight[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    static const int king[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    static const int wpawn[2][2] = {{-1, 1}, {1, 1}};
    static const int bpawn[2][2] = {{-1, -1}, {1, -1}};
    for (int sq = 0; sq < 64; sq++) {
        KNIGHT_ATTACKS[sq] = step_attacks(sq, knight, 8);
        KING_ATTACKS[sq] = step_attacks(sq, king, 8);
        PAWN_ATTACKS[WHITE][sq] = step_attacks(sq, wpawn, 2);
        PAWN_ATTACKS[BLACK][sq] = step_attacks(sq, bpawn, 2);
    }
}

// ponytail: plain ray loops; swap for magic bitboards (spec v2) when nps matters.
static Bitboard ray_attacks(int sq, Bitboard occupied, const int (*dirs)[2]) {
    Bitboard b = 0;
    for (int i = 0; i < 4; i++) {
        int f = file_of(sq) + dirs[i][0], r = rank_of(sq) + dirs[i][1];
        while (on_board(f, r)) {
            Bitboard s = sq_bb(r * 8 + f);
            b |= s;
            if (occupied & s) break;
            f += dirs[i][0];
            r += dirs[i][1];
        }
    }
    return b;
}

Bitboard bishop_attacks(int sq, Bitboard occupied) {
    static const int dirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    return ray_attacks(sq, occupied, dirs);
}

Bitboard rook_attacks(int sq, Bitboard occupied) {
    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    return ray_attacks(sq, occupied, dirs);
}
