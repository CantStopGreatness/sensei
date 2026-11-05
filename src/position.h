#pragma once
#include "bitboard.h"
#include <string>
#include <vector>

// What make_move can't recompute; handed back to unmake_move.
struct Undo {
    Piece captured;
    int castling, ep, halfmove;
};

struct Position {
    Bitboard pieces[12] = {};  // one bitboard per Piece
    Bitboard occ[2] = {};      // per color
    Piece board[64];           // mailbox, for "what is on this square"
    Color side = WHITE;
    int castling = 0;          // Castling bits
    int ep = NO_SQ;            // en passant target square
    int halfmove = 0, fullmove = 1;
    uint64_t hash = 0;              // Zobrist key, updated incrementally
    std::vector<uint64_t> history;  // hash before each move made, for repetition checks

    Position() { for (auto& p : board) p = NO_PIECE; }

    // Returns false (and leaves *this untouched) on malformed or illegal FEN.
    bool set_fen(const std::string& fen);
    std::string fen() const;
    std::string pretty() const;

    Bitboard all() const { return occ[WHITE] | occ[BLACK]; }
    int king_sq(Color c) const { return lsb(pieces[make_piece(c, KING)]); }
    bool attacked(int sq, Color by) const;
    bool in_check() const { return attacked(king_sq(side), !side); }
    // True if this position occurred before since the last capture or pawn move.
    bool is_repetition() const;
    uint64_t compute_hash() const;  // from scratch; make_move keeps `hash` equal to this

    void make_move(Move m, Undo& u);
    void unmake_move(Move m, const Undo& u);

private:
    void put(Piece p, int sq);
    void remove(int sq);
    void move_piece(int from, int to);
};

constexpr const char* START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
