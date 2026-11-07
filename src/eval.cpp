#include "eval.h"
#include <algorithm>

// Starting weights. Piece-square tables from the Chess Programming Wiki "Simplified Evaluation Function".
// Written as you'd see the board from White's side: first row is rank 8.
static const int PST0[6][64] = {
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

static const int KING_ENDGAME0[64] = {
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
constexpr int MOBILITY_BASE[6] = {0, 4, 7, 7, 14, 0};
// Game phase: 24 with all minor and major pieces on the board, 0 with none.
constexpr int PHASE_WEIGHT[6] = {0, 1, 1, 2, 4, 0};

EvalParams defaults() {
    EvalParams p{};
    for (int t = 0; t < 6; t++) {
        p.piece_mg[t] = p.piece_eg[t] = PIECE_VALUE[t];
        for (int sq = 0; sq < 64; sq++) {
            p.pst_mg[t][sq] = PST0[t][sq];
            p.pst_eg[t][sq] = t == KING ? KING_ENDGAME0[sq] : PST0[t][sq];
        }
    }
    const int passed_mg[8] = {0, 5, 10, 15, 25, 40, 60, 0}, passed_eg[8] = {0, 10, 15, 30, 50, 80, 120, 0};
    for (int r = 0; r < 8; r++) p.passed_mg[r] = passed_mg[r], p.passed_eg[r] = passed_eg[r];
    p.doubled_mg = -10, p.doubled_eg = -20;
    p.isolated_mg = -10, p.isolated_eg = -15;
    p.bishop_pair_mg = 30, p.bishop_pair_eg = 50;
    p.rook_open_mg = p.rook_open_eg = 20;
    p.rook_semi_open_mg = p.rook_semi_open_eg = 10;
    const int mob_mg[6] = {0, 4, 5, 2, 1, 0}, mob_eg[6] = {0, 4, 5, 4, 2, 0}, units[6] = {0, 2, 2, 3, 5, 0};
    for (int t = 0; t < 6; t++) p.mobility_mg[t] = mob_mg[t], p.mobility_eg[t] = mob_eg[t], p.attack_units[t] = units[t];
    p.attack_scale = 2, p.attack_cap = 500;
    p.shield_near = 12, p.shield_far = 6;
    return p;
}

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

EvalParams EVAL = defaults();

int evaluate(const Position& pos) {
    const EvalParams& P = EVAL;
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
        mg[c] += P.piece_mg[t] + P.pst_mg[t][idx];
        eg[c] += P.piece_eg[t] + P.pst_eg[t][idx];
        phase += PHASE_WEIGHT[t];

        int f = sq % 8;
        if (t == PAWN) {
            if (!(M.passed[c][sq] & pawns[!c])) {
                int rel_rank = c == WHITE ? sq / 8 : 7 - sq / 8;
                mg[c] += P.passed_mg[rel_rank];
                eg[c] += P.passed_eg[rel_rank];
            }
            if (!(M.adjacent[f] & pawns[c])) mg[c] += P.isolated_mg, eg[c] += P.isolated_eg;
        }
        if (t >= KNIGHT && t <= QUEEN) {
            Bitboard att = t == KNIGHT ? KNIGHT_ATTACKS[sq]
                         : t == BISHOP ? bishop_attacks(sq, occ)
                         : t == ROOK   ? rook_attacks(sq, occ)
                                       : bishop_attacks(sq, occ) | rook_attacks(sq, occ);
            int mob = popcount(att & ~pos.occ[c] & ~pawn_attacks[!c]) - MOBILITY_BASE[t];
            mg[c] += P.mobility_mg[t] * mob;
            eg[c] += P.mobility_eg[t] * mob;
            if (att & king_zone[!c]) attack_units[c] += P.attack_units[t], attackers[c]++;
        }
        if (t == ROOK) {
            if (!(M.file[f] & (pawns[WHITE] | pawns[BLACK]))) mg[c] += P.rook_open_mg, eg[c] += P.rook_open_eg;
            else if (!(M.file[f] & pawns[c])) mg[c] += P.rook_semi_open_mg, eg[c] += P.rook_semi_open_eg;
        }
    }
    for (Color c : {WHITE, BLACK}) {
        for (int f = 0; f < 8; f++) {
            int n = popcount(M.file[f] & pawns[c]);
            if (n > 1) mg[c] += P.doubled_mg * (n - 1), eg[c] += P.doubled_eg * (n - 1);
        }
        // King safety, middlegame only: pawn shelter, and a penalty that grows with the square of
        // the enemy firepower on the king's surroundings once at least two pieces join in.
        int ksq = king_sq[c];
        int ahead2 = c == WHITE ? ksq + 8 : ksq - 8;
        mg[c] += P.shield_near * popcount(M.shield[c][ksq] & pawns[c]);
        if (ahead2 >= 0 && ahead2 < 64) mg[c] += P.shield_far * popcount(M.shield[c][ahead2] & pawns[c]);
        if (attackers[!c] >= 2) {
            int u = attack_units[!c];
            mg[c] -= std::min(P.attack_scale * u * u, P.attack_cap);
        }
        if (popcount(pos.pieces[make_piece(c, BISHOP)]) >= 2) mg[c] += P.bishop_pair_mg, eg[c] += P.bishop_pair_eg;
    }

    phase = phase > 24 ? 24 : phase;  // early promotions can push it past 24
    int mg_score = mg[WHITE] - mg[BLACK], eg_score = eg[WHITE] - eg[BLACK];
    int score = (mg_score * phase + eg_score * (24 - phase)) / 24;
    return pos.side == WHITE ? score : -score;
}
