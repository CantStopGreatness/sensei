#include "search.h"
#include "eval.h"
#include "movegen.h"
#include "tt.h"
#include "uci.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <sstream>

int see(const Position& pos, Move m) {
    int from = from_sq(m), to = to_sq(m);
    Color side = color_of(pos.board[from]);
    Bitboard occ = pos.all() ^ sq_bb(from);
    int gain[32];
    if (flags_of(m) == EP_CAPTURE) {
        gain[0] = PIECE_VALUE[PAWN];
        occ ^= sq_bb(to + (side == WHITE ? -8 : 8));
    } else {
        gain[0] = pos.board[to] == NO_PIECE ? 0 : PIECE_VALUE[type_of(pos.board[to])];
    }
    PieceType on_square = type_of(pos.board[from]);
    const Bitboard* P = pos.pieces;
    Bitboard diag = P[WB] | P[BB] | P[WQ] | P[BQ], straight = P[WR] | P[BR] | P[WQ] | P[BQ];
    Bitboard attackers = (PAWN_ATTACKS[BLACK][to] & P[WP]) | (PAWN_ATTACKS[WHITE][to] & P[BP]) |
                         (KNIGHT_ATTACKS[to] & (P[WN] | P[BN])) | (KING_ATTACKS[to] & (P[WK] | P[BK])) |
                         (bishop_attacks(to, occ) & diag) | (rook_attacks(to, occ) & straight);
    int d = 0;
    for (side = !side;; side = !side) {
        attackers &= occ;
        Bitboard ours = attackers & pos.occ[side];
        if (!ours) break;
        int t = PAWN;
        Bitboard bb = 0;
        for (; t <= KING; t++)
            if ((bb = ours & P[make_piece(side, PieceType(t))])) break;
        if (t == KING && (attackers & pos.occ[!side])) break;  // the king can't take a defended piece
        d++;
        gain[d] = PIECE_VALUE[on_square] - gain[d - 1];
        on_square = PieceType(t);
        occ ^= sq_bb(lsb(bb));
        attackers |= (bishop_attacks(to, occ) & diag) | (rook_attacks(to, occ) & straight);  // x-rays
    }
    while (d) {
        gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
        d--;
    }
    return gain[0];
}

namespace {

using Clock = std::chrono::steady_clock;

// Late-move reduction in plies, by [depth][move index]; grows with both.
const struct LmrTable {
    int t[64][64];
    LmrTable() {
        for (int d = 0; d < 64; d++)
            for (int i = 0; i < 64; i++)
                t[d][i] = d && i ? int(0.75 + std::log(d) * std::log(i) / 2.25) : 0;
    }
    const int* operator[](int d) const { return t[d]; }
} LMR;


struct Searcher {
    Position& pos;
    const std::atomic<bool>& stop_flag;
    Clock::time_point start;
    int64_t time_ms;
    const std::atomic<bool>* pondering;
    bool ponder_active = pondering && *pondering;  // time limits are off until ponderhit
    bool aborted = false;
    bool can_abort = false;  // false during depth 1 so there is always a move
    uint64_t nodes = 0;
    Move root_best = 0;      // best move of the previous iteration, searched first
    Move killers[MAX_PLY][2] = {};
    int history[2][64][64] = {};     // [side][from][to]: quiet moves that caused cutoffs
    Move pv[MAX_PLY][MAX_PLY] = {};  // triangular PV table
    int pv_len[MAX_PLY] = {};

    int64_t elapsed_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    }

    // On ponderhit the opponent played the expected move: start the clock now.
    void check_ponderhit() {
        if (ponder_active && !*pondering) {
            ponder_active = false;
            start = Clock::now();
        }
    }

    bool should_stop() {
        if (aborted) return true;
        if (!can_abort || (nodes & 2047)) return false;
        check_ponderhit();
        aborted = stop_flag || (!ponder_active && time_ms >= 0 && elapsed_ms() >= time_ms);
        return aborted;
    }

    // Higher = searched earlier: hash move, captures by MVV-LVA, promotions, killers.
    int score_move(Move m, int ply, Move hash_move) const {
        if (m == hash_move) return 1000000;
        if (is_capture(m)) {
            int victim = flags_of(m) == EP_CAPTURE ? PAWN : type_of(pos.board[to_sq(m)]);
            int attacker = type_of(pos.board[from_sq(m)]);
            int mvv_lva = 10 * PIECE_VALUE[victim] - PIECE_VALUE[attacker] + (is_promo(m) ? 900 : 0);
            // Captures that lose material go after the quiet moves.
            return (is_promo(m) || see(pos, m) >= 0 ? 100000 : -100000) + mvv_lva;
        }
        if (is_promo(m)) return 95000 + PIECE_VALUE[promo_type(m)];
        if (m == killers[ply][0]) return 90000;
        if (m == killers[ply][1]) return 80000;
        return history[pos.side][from_sq(m)][to_sq(m)];  // kept below 80000 by aging
    }

    void order(MoveList& list, int ply, Move hash_move = 0) const {
        int scores[256];
        for (int i = 0; i < list.size; i++) scores[i] = score_move(list.moves[i], ply, hash_move);
        // Insertion sort, descending; lists are short.
        for (int i = 1; i < list.size; i++) {
            Move m = list.moves[i];
            int s = scores[i], j = i - 1;
            for (; j >= 0 && scores[j] < s; j--) scores[j + 1] = scores[j], list.moves[j + 1] = list.moves[j];
            scores[j + 1] = s, list.moves[j + 1] = m;
        }
    }

    // Captures (and check evasions) only, until the position is quiet.
    int quiescence(int alpha, int beta, int ply) {
        nodes++;
        if (should_stop()) return 0;
        bool in_check = pos.in_check();
        if (ply >= MAX_PLY - 1) return evaluate(pos);

        int best = -MATE + ply;  // only reachable if in check with no moves = mated
        if (!in_check) {
            best = evaluate(pos);  // "stand pat": we don't have to capture
            if (best >= beta) return best;
            if (best > alpha) alpha = best;
        }

        MoveList list;
        generate_legal(pos, list, !in_check);  // in check, every evasion counts
        if (in_check && list.size == 0) return -MATE + ply;
        order(list, ply);
        for (Move m : list) {
            if (!in_check && !is_capture(m) && !(is_promo(m) && promo_type(m) == QUEEN)) continue;
            if (!in_check && !is_promo(m) && see(pos, m) < 0) continue;  // losing capture
            Undo u;
            pos.make_move(m, u);
            int score = -quiescence(-beta, -alpha, ply + 1);
            pos.unmake_move(m, u);
            if (aborted) return 0;
            if (score > best) best = score;
            if (score > alpha) alpha = score;
            if (alpha >= beta) break;
        }
        return best;
    }

    int negamax(int depth, int alpha, int beta, int ply, bool allow_null = true) {
        pv_len[ply] = 0;
        if (ply > 0 && (pos.halfmove >= 100 || pos.is_repetition())) return 0;
        bool in_check = pos.in_check();
        if (in_check) depth++;  // check extension: don't let a check push mate past the horizon
        if (depth <= 0) return quiescence(alpha, beta, ply);
        nodes++;
        if (should_stop()) return 0;
        if (ply >= MAX_PLY - 1) return evaluate(pos);

        // A good-enough earlier result for this position ends the search here. Not at the
        // root, which must always produce a move and a PV.
        Move hash_move = ply == 0 ? root_best : 0;
        if (const TTEntry* e = TT.probe(pos.hash)) {
            if (ply > 0) hash_move = e->move;
            int score = score_from_tt(e->score, ply);
            if (ply > 0 && e->depth >= depth &&
                (e->bound == BOUND_EXACT || (e->bound == BOUND_LOWER && score >= beta) ||
                 (e->bound == BOUND_UPPER && score <= alpha)))
                return score;
        }

        bool pv_node = beta - alpha > 1;
        int static_eval = in_check ? -MATE : evaluate(pos);
        bool no_mates = std::abs(alpha) < MATE - MAX_PLY && std::abs(beta) < MATE - MAX_PLY;

        // Reverse futility pruning: so far above beta that a shallow search won't bring it back.
        if (!pv_node && !in_check && ply > 0 && depth <= 6 && no_mates && static_eval - 80 * depth >= beta)
            return static_eval;

        // Null-move pruning: if passing still leaves us >= beta after a shallower search,
        // a real move will too. Unsound in zugzwang, so skip with only king and pawns.
        Color us = pos.side;
        bool has_pieces = pos.occ[us] & ~(pos.pieces[make_piece(us, PAWN)] | pos.pieces[make_piece(us, KING)]);
        if (allow_null && ply > 0 && !in_check && depth >= 3 && has_pieces && beta < MATE - MAX_PLY &&
            static_eval >= beta) {
            int r = depth > 6 ? 3 : 2;
            Undo u;
            pos.make_null(u);
            int score = -negamax(depth - 1 - r, -beta, -beta + 1, ply + 1, false);
            pos.unmake_null(u);
            if (aborted) return 0;
            if (score >= beta) return score >= MATE - MAX_PLY ? beta : score;  // don't trust null-move mates
        }

        MoveList list;
        generate_legal(pos, list);
        if (list.size == 0) return in_check ? -MATE + ply : 0;  // checkmate or stalemate
        order(list, ply, hash_move);

        // Futility pruning: near the leaves, quiet moves can't lift a hopeless static eval to alpha.
        bool futile = !pv_node && !in_check && depth <= 2 && no_mates && static_eval + 120 * depth <= alpha;

        int alpha_orig = alpha, best = -MATE;
        Move best_move = 0;
        for (int i = 0; i < list.size; i++) {
            Move m = list.moves[i];
            bool quiet = !is_capture(m) && !is_promo(m);
            Undo u;
            pos.make_move(m, u);
            if (futile && i > 0 && quiet && !pos.in_check()) {
                pos.unmake_move(m, u);
                continue;
            }
            int score;
            if (i == 0) {
                score = -negamax(depth - 1, -beta, -alpha, ply + 1);
            } else {
                // PVS: later moves only need to prove they're no better than alpha (null window).
                // LMR: late quiet moves are probably bad, so prove that at reduced depth first.
                // Never at the root, and one ply less on the PV, where a miss costs the most.
                int r = 0;
                if (ply > 0 && depth >= 3 && i >= 3 && quiet && !in_check && !pos.in_check() &&
                    m != killers[ply][0] && m != killers[ply][1])
                    r = std::max(0, std::min(LMR[std::min(depth, 63)][std::min(i, 63)] - (beta - alpha > 1),
                                             depth - 2));
                score = -negamax(depth - 1 - r, -alpha - 1, -alpha, ply + 1);
                if (score > alpha && r > 0) score = -negamax(depth - 1, -alpha - 1, -alpha, ply + 1);
                if (score > alpha && score < beta) score = -negamax(depth - 1, -beta, -alpha, ply + 1);
            }
            pos.unmake_move(m, u);
            if (aborted) return 0;

            if (score > best) best = score;
            if (score > alpha) {
                alpha = score;
                best_move = m;
                pv[ply][0] = m;
                for (int i = 0; i < pv_len[ply + 1]; i++) pv[ply][i + 1] = pv[ply + 1][i];
                pv_len[ply] = pv_len[ply + 1] + 1;
            }
            if (alpha >= beta) {
                if (quiet) {
                    if (m != killers[ply][0]) {
                        killers[ply][1] = killers[ply][0];
                        killers[ply][0] = m;
                    }
                    int& h = history[pos.side][from_sq(m)][to_sq(m)];
                    h += depth * depth;
                    if (h > 60000)  // age: halve everything so recent cutoffs dominate
                        for (auto& side : history) for (auto& row : side) for (int& v : row) v /= 2;
                }
                break;
            }
        }
        Bound bound = best >= beta ? BOUND_LOWER : alpha > alpha_orig ? BOUND_EXACT : BOUND_UPPER;
        TT.store(pos.hash, depth, best, bound, best_move, ply);
        return best;
    }
};

std::string score_string(int score) {
    if (score > MATE - MAX_PLY) return "mate " + std::to_string((MATE - score + 1) / 2);
    if (score < -MATE + MAX_PLY) return "mate -" + std::to_string((MATE + score) / 2);
    return "cp " + std::to_string(score);
}

}  // namespace

SearchResult search(Position& pos, const SearchLimits& limits, const std::atomic<bool>& stop, std::ostream& out) {
    Searcher s{pos, stop, Clock::now(), limits.time_ms, limits.pondering};
    SearchResult result;
    int stable = 0;  // iterations in a row the best move hasn't changed

    for (int depth = 1; depth <= limits.depth; depth++) {
        s.can_abort = depth > 1;
        // Aspiration window: expect a score near the last one; widen and re-search on a miss.
        int score, delta = 25;
        int alpha = -MATE - 1, beta = MATE + 1;
        if (depth >= 5 && std::abs(result.score) < MATE - MAX_PLY)
            alpha = result.score - delta, beta = result.score + delta;
        while (true) {
            score = s.negamax(depth, alpha, beta, 0);
            if (s.aborted) break;
            if (score <= alpha) alpha = std::max(score - delta, -MATE - 1);
            else if (score >= beta) beta = std::min(score + delta, MATE + 1);
            else break;
            delta *= 2;
        }
        if (s.aborted) break;  // partial iteration: keep the last complete one
        if (s.pv_len[0] == 0) break;  // no legal moves
        stable = s.pv[0][0] == result.best ? stable + 1 : 0;
        result = {s.pv[0][0], score, depth, s.nodes, s.pv_len[0] >= 2 ? s.pv[0][1] : Move(0)};
        s.root_best = result.best;

        int64_t ms = s.elapsed_ms();
        std::ostringstream info;
        info << "info depth " << depth << " score " << score_string(score) << " nodes " << s.nodes
             << " nps " << s.nodes * 1000 / (ms > 0 ? ms : 1) << " time " << ms << " pv";
        for (int i = 0; i < s.pv_len[0]; i++) info << ' ' << move_to_uci(s.pv[0][i]);
        say(out, info.str());

        // Stop on a mate only once the full-width search covers it: then no shorter mate exists.
        // (A warm TT or check extensions can show a longer mate at a shallower depth.)
        if (MATE - std::abs(score) <= depth) break;
        s.check_ponderhit();
        if (s.ponder_active) continue;  // no clock yet: keep deepening until ponderhit or stop
        // The next depth takes about twice as long as everything so far; don't start what can't
        // finish by the target. Spend longer while the best move keeps changing, less once it's settled.
        if (limits.soft_ms >= 0) {
            double factor = stable == 0 ? 1.5 : stable >= 4 ? 0.6 : 1.0;
            if (ms * 2 > limits.soft_ms * factor) break;
        } else if (limits.time_ms >= 0 && ms * 2 > limits.time_ms) {
            break;
        }
    }
    result.nodes = s.nodes;
    return result;
}
