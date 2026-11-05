#include "position.h"
#include <sstream>
#include <vector>

static const std::string PIECE_CHARS = "PNBRQKpnbrqk";

static const struct Zobrist {
    uint64_t piece[12][64], castling[16], ep[65], side;
    Zobrist() {
        uint64_t x = 0x9E3779B97F4A7C15ULL;
        auto next = [&x] {  // splitmix64
            uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            return z ^ (z >> 31);
        };
        for (auto& p : piece) for (auto& k : p) k = next();
        for (auto& k : castling) k = next();
        for (auto& k : ep) k = next();
        ep[NO_SQ] = 0;
        side = next();
    }
} ZOBRIST;

uint64_t Position::compute_hash() const {
    uint64_t h = ZOBRIST.castling[castling] ^ ZOBRIST.ep[ep] ^ (side == BLACK ? ZOBRIST.side : 0);
    for (int sq = 0; sq < 64; sq++)
        if (board[sq] != NO_PIECE) h ^= ZOBRIST.piece[board[sq]][sq];
    return h;
}

bool Position::is_repetition() const {
    int n = int(history.size());
    for (int i = n - 2; i >= 0 && i >= n - halfmove; i -= 2)
        if (history[i] == hash) return true;
    return false;
}

void Position::put(Piece p, int sq) {
    pieces[p] |= sq_bb(sq);
    occ[color_of(p)] |= sq_bb(sq);
    board[sq] = p;
    hash ^= ZOBRIST.piece[p][sq];
}

void Position::remove(int sq) {
    Piece p = board[sq];
    pieces[p] &= ~sq_bb(sq);
    occ[color_of(p)] &= ~sq_bb(sq);
    board[sq] = NO_PIECE;
    hash ^= ZOBRIST.piece[p][sq];
}

void Position::move_piece(int from, int to) {
    Piece p = board[from];
    remove(from);
    put(p, to);
}

static int parse_square(const std::string& s) {
    if (s.size() != 2 || s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return -1;
    return (s[1] - '1') * 8 + (s[0] - 'a');
}

static bool parse_uint(const std::string& s, int& out) {
    if (s.empty() || s.size() > 6) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    out = std::stoi(s);
    return true;
}

bool Position::set_fen(const std::string& fen) {
    std::istringstream ss(fen);
    std::vector<std::string> f;
    for (std::string t; ss >> t;) f.push_back(t);
    if (f.size() != 4 && f.size() != 6) return false;  // counters are optional

    Position p;
    int rank = 7, file = 0;
    for (char c : f[0]) {
        if (c == '/') {
            if (file != 8 || rank == 0) return false;
            rank--, file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return false;
        } else {
            size_t idx = PIECE_CHARS.find(c);
            if (idx == std::string::npos || file >= 8) return false;
            p.put(Piece(idx), rank * 8 + file++);
        }
    }
    if (rank != 0 || file != 8) return false;
    if (popcount(p.pieces[WK]) != 1 || popcount(p.pieces[BK]) != 1) return false;
    if ((p.pieces[WP] | p.pieces[BP]) & (RANK_1 | RANK_8)) return false;

    if (f[1] == "w") p.side = WHITE;
    else if (f[1] == "b") p.side = BLACK;
    else return false;

    if (f[2] != "-") {
        for (char c : f[2]) {
            size_t idx = std::string("KQkq").find(c);
            if (idx == std::string::npos || (p.castling & (1 << idx))) return false;
            p.castling |= 1 << idx;
        }
    }
    // Castling rights need king and rook on their home squares.
    if ((p.castling & WK_CASTLE) && (p.board[4] != WK || p.board[7] != WR)) return false;
    if ((p.castling & WQ_CASTLE) && (p.board[4] != WK || p.board[0] != WR)) return false;
    if ((p.castling & BK_CASTLE) && (p.board[60] != BK || p.board[63] != BR)) return false;
    if ((p.castling & BQ_CASTLE) && (p.board[60] != BK || p.board[56] != BR)) return false;

    if (f[3] != "-") {
        int sq = parse_square(f[3]);
        if (sq < 0 || rank_of(sq) != (p.side == WHITE ? 5 : 2)) return false;
        p.ep = sq;
    }

    if (f.size() == 6 && (!parse_uint(f[4], p.halfmove) || !parse_uint(f[5], p.fullmove) || p.fullmove < 1))
        return false;

    // The side that just moved can't have left its king in check.
    if (p.attacked(p.king_sq(!p.side), p.side)) return false;

    p.hash = p.compute_hash();
    *this = p;
    return true;
}

std::string Position::fen() const {
    std::string s;
    for (int rank = 7; rank >= 0; rank--) {
        int empty = 0;
        for (int file = 0; file < 8; file++) {
            Piece p = board[rank * 8 + file];
            if (p == NO_PIECE) { empty++; continue; }
            if (empty) s += char('0' + empty), empty = 0;
            s += PIECE_CHARS[p];
        }
        if (empty) s += char('0' + empty);
        if (rank) s += '/';
    }
    s += side == WHITE ? " w " : " b ";
    std::string c;
    for (int i = 0; i < 4; i++) if (castling & (1 << i)) c += "KQkq"[i];
    s += c.empty() ? "-" : c;
    s += ' ';
    s += ep == NO_SQ ? "-" : square_name(ep);
    s += ' ' + std::to_string(halfmove) + ' ' + std::to_string(fullmove);
    return s;
}

std::string Position::pretty() const {
    std::ostringstream o;
    const char* line = "  +---+---+---+---+---+---+---+---+\n";
    o << line;
    for (int rank = 7; rank >= 0; rank--) {
        o << rank + 1 << ' ';
        for (int file = 0; file < 8; file++) {
            Piece p = board[rank * 8 + file];
            o << "| " << (p == NO_PIECE ? ' ' : PIECE_CHARS[p]) << ' ';
        }
        o << "|\n" << line;
    }
    o << "    a   b   c   d   e   f   g   h\n\n";
    o << "FEN: " << fen() << '\n';
    if (in_check()) o << (side == WHITE ? "White" : "Black") << " is in check\n";
    return o.str();
}

bool Position::attacked(int sq, Color by) const {
    Bitboard all_occ = all();
    // A `by` pawn attacks sq iff a pawn of the other color on sq would attack it back.
    if (PAWN_ATTACKS[!by][sq] & pieces[make_piece(by, PAWN)]) return true;
    if (KNIGHT_ATTACKS[sq] & pieces[make_piece(by, KNIGHT)]) return true;
    if (KING_ATTACKS[sq] & pieces[make_piece(by, KING)]) return true;
    Bitboard queens = pieces[make_piece(by, QUEEN)];
    if (bishop_attacks(sq, all_occ) & (pieces[make_piece(by, BISHOP)] | queens)) return true;
    if (rook_attacks(sq, all_occ) & (pieces[make_piece(by, ROOK)] | queens)) return true;
    return false;
}

// castling &= CASTLE_MASK[from] & CASTLE_MASK[to] clears rights when a king or rook
// moves away from, or is captured on, its home square.
static const struct CastleMask {
    int m[64];
    CastleMask() {
        for (int& x : m) x = 15;
        m[0] = ~WQ_CASTLE & 15;
        m[4] = ~(WK_CASTLE | WQ_CASTLE) & 15;
        m[7] = ~WK_CASTLE & 15;
        m[56] = ~BQ_CASTLE & 15;
        m[60] = ~(BK_CASTLE | BQ_CASTLE) & 15;
        m[63] = ~BK_CASTLE & 15;
    }
} CASTLE_MASK;

void Position::make_move(Move m, Undo& u) {
    int from = from_sq(m), to = to_sq(m), fl = flags_of(m);
    Piece moving = board[from];
    u = {NO_PIECE, castling, ep, halfmove};
    history.push_back(hash);
    hash ^= ZOBRIST.castling[castling] ^ ZOBRIST.ep[ep] ^ ZOBRIST.side;

    if (fl == EP_CAPTURE) {
        int cap = to + (side == WHITE ? -8 : 8);
        u.captured = board[cap];
        remove(cap);
    } else if (is_capture(m)) {
        u.captured = board[to];
        remove(to);
    }

    move_piece(from, to);
    if (is_promo(m)) {
        remove(to);
        put(make_piece(side, promo_type(m)), to);
    }
    if (fl == KING_CASTLE) move_piece(to + 1, to - 1);   // h-rook to f-file
    if (fl == QUEEN_CASTLE) move_piece(to - 2, to + 1);  // a-rook to d-file

    halfmove = (type_of(moving) == PAWN || u.captured != NO_PIECE) ? 0 : halfmove + 1;
    ep = fl == DOUBLE_PUSH ? (from + to) / 2 : NO_SQ;
    castling &= CASTLE_MASK.m[from] & CASTLE_MASK.m[to];
    hash ^= ZOBRIST.castling[castling] ^ ZOBRIST.ep[ep];
    if (side == BLACK) fullmove++;
    side = !side;
}

void Position::unmake_move(Move m, const Undo& u) {
    side = !side;
    if (side == BLACK) fullmove--;
    int from = from_sq(m), to = to_sq(m), fl = flags_of(m);

    if (fl == KING_CASTLE) move_piece(to - 1, to + 1);
    if (fl == QUEEN_CASTLE) move_piece(to + 1, to - 2);
    if (is_promo(m)) {
        remove(to);
        put(make_piece(side, PAWN), to);
    }
    move_piece(to, from);
    if (u.captured != NO_PIECE)
        put(u.captured, fl == EP_CAPTURE ? to + (side == WHITE ? -8 : 8) : to);

    castling = u.castling;
    ep = u.ep;
    halfmove = u.halfmove;
    hash = history.back();
    history.pop_back();
}
