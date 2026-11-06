#include "movegen.h"

static void add_pawn_move(MoveList& list, int from, int to, bool capture) {
    int cap = capture ? CAPTURE : 0;
    if (sq_bb(to) & (RANK_1 | RANK_8)) {
        for (int pt = QUEEN; pt >= KNIGHT; pt--) list.add(make_move(from, to, PROMO | cap | (pt - KNIGHT)));
    } else {
        list.add(make_move(from, to, cap));
    }
}

static void add_targets(MoveList& list, int from, Bitboard targets, Bitboard enemy) {
    while (targets) {
        int to = pop_lsb(targets);
        list.add(make_move(from, to, (enemy & sq_bb(to)) ? CAPTURE : QUIET));
    }
}

void generate_pseudo_legal(const Position& pos, MoveList& list, bool captures_only) {
    Color us = pos.side, them = !us;
    Bitboard own = pos.occ[us], enemy = pos.occ[them], all = pos.all();
    Bitboard targets = captures_only ? enemy : ~own;

    // Pawns
    int up = us == WHITE ? 8 : -8;
    Bitboard start_rank = us == WHITE ? RANK_1 << 8 : RANK_1 << 48;
    for (Bitboard b = pos.pieces[make_piece(us, PAWN)]; b;) {
        int from = pop_lsb(b), to = from + up;
        bool promotes = sq_bb(to) & (RANK_1 | RANK_8);
        if (!(all & sq_bb(to)) && (!captures_only || promotes)) {
            add_pawn_move(list, from, to, false);
            if (!captures_only && (sq_bb(from) & start_rank) && !(all & sq_bb(to + up)))
                list.add(make_move(from, to + up, DOUBLE_PUSH));
        }
        for (Bitboard caps = PAWN_ATTACKS[us][from] & enemy; caps;)
            add_pawn_move(list, from, pop_lsb(caps), true);
        if (pos.ep != NO_SQ && (PAWN_ATTACKS[us][from] & sq_bb(pos.ep)))
            list.add(make_move(from, pos.ep, EP_CAPTURE));
    }

    // Pieces
    for (Bitboard b = pos.pieces[make_piece(us, KNIGHT)]; b;) {
        int from = pop_lsb(b);
        add_targets(list, from, KNIGHT_ATTACKS[from] & targets, enemy);
    }
    for (Bitboard b = pos.pieces[make_piece(us, BISHOP)] | pos.pieces[make_piece(us, QUEEN)]; b;) {
        int from = pop_lsb(b);
        add_targets(list, from, bishop_attacks(from, all) & targets, enemy);
    }
    for (Bitboard b = pos.pieces[make_piece(us, ROOK)] | pos.pieces[make_piece(us, QUEEN)]; b;) {
        int from = pop_lsb(b);
        add_targets(list, from, rook_attacks(from, all) & targets, enemy);
    }
    int ksq = pos.king_sq(us);
    add_targets(list, ksq, KING_ATTACKS[ksq] & targets, enemy);
    if (captures_only) return;

    // Castling: path empty, king not in check and doesn't pass through an attacked
    // square. The landing square is checked by the legality filter.
    int base = us == WHITE ? 0 : 56;  // a1 or a8
    int ks = us == WHITE ? WK_CASTLE : BK_CASTLE, qs = us == WHITE ? WQ_CASTLE : BQ_CASTLE;
    if ((pos.castling & ks) && !(all & (sq_bb(base + 5) | sq_bb(base + 6))) &&
        !pos.attacked(base + 4, them) && !pos.attacked(base + 5, them))
        list.add(make_move(base + 4, base + 6, KING_CASTLE));
    if ((pos.castling & qs) && !(all & (sq_bb(base + 1) | sq_bb(base + 2) | sq_bb(base + 3))) &&
        !pos.attacked(base + 4, them) && !pos.attacked(base + 3, them))
        list.add(make_move(base + 4, base + 2, QUEEN_CASTLE));
}

// ponytail: legality by make/test/unmake; pin + check-mask generation is faster if nps matters.
void generate_legal(Position& pos, MoveList& list, bool captures_only) {
    MoveList pseudo;
    generate_pseudo_legal(pos, pseudo, captures_only);
    Color us = pos.side;
    for (Move m : pseudo) {
        Undo u;
        pos.make_move(m, u);
        if (!pos.attacked(pos.king_sq(us), !us)) list.add(m);
        pos.unmake_move(m, u);
    }
}
