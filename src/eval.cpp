#include "eval.h"

// Piece-square tables from the Chess Programming Wiki "Simplified Evaluation Function".
// Written as you'd see the board from White's side: first row is rank 8.
static const int PST[6][64] = {
    {  0,  0,  0,  0,  0,  0,  0,  0,
      50, 50, 50, 50, 50, 50, 50, 50,
      10, 10, 20, 30, 30, 20, 10, 10,
       5,  5, 10, 25, 25, 10,  5,  5,
       0,  0,  0, 20, 20,  0,  0,  0,
       5, -5,-10,  0,  0,-10, -5,  5,
       5, 10, 10,-20,-20, 10, 10,  5,
       0,  0,  0,  0,  0,  0,  0,  0},
    {-50,-40,-30,-30,-30,-30,-40,-50,
     -40,-20,  0,  0,  0,  0,-20,-40,
     -30,  0, 10, 15, 15, 10,  0,-30,
     -30,  5, 15, 20, 20, 15,  5,-30,
     -30,  0, 15, 20, 20, 15,  0,-30,
     -30,  5, 10, 15, 15, 10,  5,-30,
     -40,-20,  0,  5,  5,  0,-20,-40,
     -50,-40,-30,-30,-30,-30,-40,-50},
    {-20,-10,-10,-10,-10,-10,-10,-20,
     -10,  0,  0,  0,  0,  0,  0,-10,
     -10,  0,  5, 10, 10,  5,  0,-10,
     -10,  5,  5, 10, 10,  5,  5,-10,
     -10,  0, 10, 10, 10, 10,  0,-10,
     -10, 10, 10, 10, 10, 10, 10,-10,
     -10,  5,  0,  0,  0,  0,  5,-10,
     -20,-10,-10,-10,-10,-10,-10,-20},
    {  0,  0,  0,  0,  0,  0,  0,  0,
       5, 10, 10, 10, 10, 10, 10,  5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
       0,  0,  0,  5,  5,  0,  0,  0},
    {-20,-10,-10, -5, -5,-10,-10,-20,
     -10,  0,  0,  0,  0,  0,  0,-10,
     -10,  0,  5,  5,  5,  5,  0,-10,
      -5,  0,  5,  5,  5,  5,  0, -5,
       0,  0,  5,  5,  5,  5,  0, -5,
     -10,  5,  5,  5,  5,  5,  0,-10,
     -10,  0,  5,  0,  0,  0,  0,-10,
     -20,-10,-10, -5, -5,-10,-10,-20},
    {-30,-40,-40,-50,-50,-40,-40,-30,  // king, middlegame: stay castled
     -30,-40,-40,-50,-50,-40,-40,-30,
     -30,-40,-40,-50,-50,-40,-40,-30,
     -30,-40,-40,-50,-50,-40,-40,-30,
     -20,-30,-30,-40,-40,-30,-30,-20,
     -10,-20,-20,-20,-20,-20,-20,-10,
      20, 20,  0,  0,  0,  0, 20, 20,
      20, 30, 10,  0,  0, 10, 30, 20},
};

static const int KING_ENDGAME[64] = {
    -50,-40,-30,-20,-20,-30,-40,-50,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -50,-30,-30,-30,-30,-30,-30,-50};

namespace {

constexpr Bitboard FILE_A = 0x0101010101010101ULL;

// Bonuses by how far a passed pawn has come (relative rank 0-7), middlegame and endgame.
constexpr int PASSED_MG[8] = {0, 5, 10, 15, 25, 40, 60, 0};
constexpr int PASSED_EG[8] = {0, 10, 15, 30, 50, 80, 120, 0};
constexpr int DOUBLED_MG = -10, DOUBLED_EG = -20;
constexpr int ISOLATED_MG = -10, ISOLATED_EG = -15;
constexpr int BISHOP_PAIR_MG = 30, BISHOP_PAIR_EG = 50;
constexpr int ROOK_OPEN = 20, ROOK_SEMI_OPEN = 10;  // same in both phases
// Mobility per reachable square beyond a typical count, by piece type.
constexpr int MOBILITY_MG[6] = {0, 4, 5, 2, 1, 0};
constexpr int MOBILITY_EG[6] = {0, 4, 5, 4, 2, 0};
constexpr int MOBILITY_BASE[6] = {0, 4, 7, 7, 14, 0};
// King safety: weight of each piece type attacking the squares around the enemy king.
constexpr int ATTACK_UNITS[6] = {0, 2, 2, 3, 5, 0};
constexpr int SHIELD_NEAR = 12, SHIELD_FAR = 6;  // own pawns one and two ranks in front of the king
// Game phase: 24 with all minor and major pieces on the board, 0 with none.
constexpr int PHASE_WEIGHT[6] = {0, 1, 1, 2, 4, 0};

const struct Masks {
    Bitboard file[8], adjacent[8];
    Bitboard shield[2][64];  // the three files around the king, one and two ranks in front
    Bitboard passed[2][64];  // squares that must be free of enemy pawns for a pawn to be passed
    Masks() {
        for (int f = 0; f < 8; f++) {
            file[f] = FILE_A << f;
            adjacent[f] = (f > 0 ? FILE_A << (f - 1) : 0) | (f < 7 ? FILE_A << (f + 1) : 0);
        }
        for (int sq = 0; sq < 64; sq++) {
            int r = sq / 8, f = sq % 8;
            Bitboard span = file[f] | adjacent[f];
            Bitboard above = r < 7 ? ~0ULL << (8 * (r + 1)) : 0;
            Bitboard below = r > 0 ? ~0ULL >> (8 * (8 - r)) : 0;
            passed[WHITE][sq] = span & above;
            passed[BLACK][sq] = span & below;
            Bitboard near_w = r < 7 ? span & (RANK_1 << (8 * (r + 1))) : 0;
            Bitboard near_b = r > 0 ? span & (RANK_1 << (8 * (r - 1))) : 0;
            shield[WHITE][sq] = near_w;
            shield[BLACK][sq] = near_b;
        }
    }
} M;

}  // namespace

int evaluate(const Position& pos) {
    int mg[2] = {}, eg[2] = {}, phase = 0;
    Bitboard pawns[2] = {pos.pieces[WP], pos.pieces[BP]};
    Bitboard occ = pos.all();
    Bitboard pawn_attacks[2] = {((pawns[WHITE] << 7) & ~(FILE_A << 7)) | ((pawns[WHITE] << 9) & ~FILE_A),
                                ((pawns[BLACK] >> 9) & ~(FILE_A << 7)) | ((pawns[BLACK] >> 7) & ~FILE_A)};
    int king_sq[2] = {pos.king_sq(WHITE), pos.king_sq(BLACK)};
    Bitboard king_zone[2] = {KING_ATTACKS[king_sq[WHITE]] | sq_bb(king_sq[WHITE]),
                             KING_ATTACKS[king_sq[BLACK]] | sq_bb(king_sq[BLACK])};
    int attack_units[2] = {}, attackers[2] = {};  // against the other side's king

    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.board[sq];
        if (p == NO_PIECE) continue;
        PieceType t = type_of(p);
        Color c = color_of(p);
        // Tables are laid out rank 8 first, so White flips the square; Black reads it mirrored.
        int idx = c == WHITE ? sq ^ 56 : sq;
        mg[c] += PIECE_VALUE[t] + PST[t][idx];
        eg[c] += PIECE_VALUE[t] + (t == KING ? KING_ENDGAME[idx] : PST[t][idx]);
        phase += PHASE_WEIGHT[t];

        int f = sq % 8;
        if (t == PAWN) {
            if (!(M.passed[c][sq] & pawns[!c])) {
                int rel_rank = c == WHITE ? sq / 8 : 7 - sq / 8;
                mg[c] += PASSED_MG[rel_rank];
                eg[c] += PASSED_EG[rel_rank];
            }
            if (!(M.adjacent[f] & pawns[c])) mg[c] += ISOLATED_MG, eg[c] += ISOLATED_EG;
        }
        if (t >= KNIGHT && t <= QUEEN) {
            Bitboard att = t == KNIGHT ? KNIGHT_ATTACKS[sq]
                         : t == BISHOP ? bishop_attacks(sq, occ)
                         : t == ROOK   ? rook_attacks(sq, occ)
                                       : bishop_attacks(sq, occ) | rook_attacks(sq, occ);
            int mob = popcount(att & ~pos.occ[c] & ~pawn_attacks[!c]) - MOBILITY_BASE[t];
            mg[c] += MOBILITY_MG[t] * mob;
            eg[c] += MOBILITY_EG[t] * mob;
            if (att & king_zone[!c]) attack_units[c] += ATTACK_UNITS[t], attackers[c]++;
        }
        if (t == ROOK) {
            if (!(M.file[f] & (pawns[WHITE] | pawns[BLACK]))) mg[c] += ROOK_OPEN, eg[c] += ROOK_OPEN;
            else if (!(M.file[f] & pawns[c])) mg[c] += ROOK_SEMI_OPEN, eg[c] += ROOK_SEMI_OPEN;
        }
    }
    for (Color c : {WHITE, BLACK}) {
        for (int f = 0; f < 8; f++) {
            int n = popcount(M.file[f] & pawns[c]);
            if (n > 1) mg[c] += DOUBLED_MG * (n - 1), eg[c] += DOUBLED_EG * (n - 1);
        }
        // King safety, middlegame only: pawn shelter, and a penalty that grows with the square of
        // the enemy firepower on the king's surroundings once at least two pieces join in.
        int ksq = king_sq[c];
        int ahead2 = c == WHITE ? ksq + 8 : ksq - 8;
        mg[c] += SHIELD_NEAR * popcount(M.shield[c][ksq] & pawns[c]);
        if (ahead2 >= 0 && ahead2 < 64) mg[c] += SHIELD_FAR * popcount(M.shield[c][ahead2] & pawns[c]);
        if (attackers[!c] >= 2) {
            int u = attack_units[!c];
            mg[c] -= u * u * 2 < 500 ? u * u * 2 : 500;
        }
        if (popcount(pos.pieces[make_piece(c, BISHOP)]) >= 2) mg[c] += BISHOP_PAIR_MG, eg[c] += BISHOP_PAIR_EG;
    }

    phase = phase > 24 ? 24 : phase;  // early promotions can push it past 24
    int mg_score = mg[WHITE] - mg[BLACK], eg_score = eg[WHITE] - eg[BLACK];
    int score = (mg_score * phase + eg_score * (24 - phase)) / 24;
    return pos.side == WHITE ? score : -score;
}
