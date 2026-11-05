#pragma once
#include <cstdint>
#include <string>

using Bitboard = uint64_t;

enum Color : int { WHITE, BLACK };
enum PieceType : int { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
enum Piece : int { WP, WN, WB, WR, WQ, WK, BP, BN, BB, BR, BQ, BK, NO_PIECE };

// Squares: a1 = 0, b1 = 1, ..., h8 = 63.
constexpr int NO_SQ = 64;
constexpr int file_of(int sq) { return sq & 7; }
constexpr int rank_of(int sq) { return sq >> 3; }

constexpr Color operator!(Color c) { return Color(c ^ 1); }
constexpr Piece make_piece(Color c, PieceType t) { return Piece(c * 6 + t); }
constexpr PieceType type_of(Piece p) { return PieceType(p % 6); }
constexpr Color color_of(Piece p) { return Color(p / 6); }

enum Castling : int { WK_CASTLE = 1, WQ_CASTLE = 2, BK_CASTLE = 4, BQ_CASTLE = 8 };

// 16-bit move: bits 0-5 from, 6-11 to, 12-15 flags.
// Flag bit 2 (value 4) = capture, bit 3 (value 8) = promotion; low 2 bits of a
// promotion give the piece (0=N, 1=B, 2=R, 3=Q).
using Move = uint16_t;
enum MoveFlag : int {
    QUIET = 0, DOUBLE_PUSH = 1, KING_CASTLE = 2, QUEEN_CASTLE = 3,
    CAPTURE = 4, EP_CAPTURE = 5, PROMO = 8, PROMO_CAPTURE = 12
};

constexpr Move make_move(int from, int to, int flags) { return Move(from | (to << 6) | (flags << 12)); }
constexpr int from_sq(Move m) { return m & 63; }
constexpr int to_sq(Move m) { return (m >> 6) & 63; }
constexpr int flags_of(Move m) { return m >> 12; }
constexpr bool is_capture(Move m) { return flags_of(m) & CAPTURE; }
constexpr bool is_promo(Move m) { return flags_of(m) & PROMO; }
constexpr PieceType promo_type(Move m) { return PieceType(KNIGHT + (flags_of(m) & 3)); }

inline std::string square_name(int sq) {
    return {char('a' + file_of(sq)), char('1' + rank_of(sq))};
}

inline std::string move_to_uci(Move m) {
    std::string s = square_name(from_sq(m)) + square_name(to_sq(m));
    if (is_promo(m)) s += "nbrq"[promo_type(m) - KNIGHT];
    return s;
}
