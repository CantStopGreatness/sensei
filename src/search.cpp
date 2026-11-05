#include "search.h"
#include "eval.h"
#include "movegen.h"
#include <chrono>
#include <sstream>

namespace {

using Clock = std::chrono::steady_clock;

struct Searcher {
    Position& pos;
    const std::atomic<bool>& stop_flag;
    Clock::time_point start;
    int64_t time_ms;
    bool aborted = false;
    bool can_abort = false;  // false during depth 1 so there is always a move
    uint64_t nodes = 0;
    Move root_best = 0;      // best move of the previous iteration, searched first
    Move killers[MAX_PLY][2] = {};
    Move pv[MAX_PLY][MAX_PLY] = {};  // triangular PV table
    int pv_len[MAX_PLY] = {};

    int64_t elapsed_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    }

    bool should_stop() {
        if (aborted) return true;
        if (!can_abort || (nodes & 2047)) return false;
        aborted = stop_flag || (time_ms >= 0 && elapsed_ms() >= time_ms);
        return aborted;
    }

    // Higher = searched earlier: previous best at the root, captures by MVV-LVA, killers.
    int score_move(Move m, int ply) const {
        if (ply == 0 && m == root_best) return 1000000;
        if (is_capture(m)) {
            int victim = flags_of(m) == EP_CAPTURE ? PAWN : type_of(pos.board[to_sq(m)]);
            int attacker = type_of(pos.board[from_sq(m)]);
            return 100000 + 10 * PIECE_VALUE[victim] - PIECE_VALUE[attacker] + (is_promo(m) ? 900 : 0);
        }
        if (is_promo(m)) return 95000 + PIECE_VALUE[promo_type(m)];
        if (m == killers[ply][0]) return 90000;
        if (m == killers[ply][1]) return 80000;
        return 0;
    }

    void order(MoveList& list, int ply) const {
        int scores[256];
        for (int i = 0; i < list.size; i++) scores[i] = score_move(list.moves[i], ply);
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

        // ponytail: generates all legal moves then filters; a captures-only generator is faster.
        MoveList list;
        generate_legal(pos, list);
        if (in_check && list.size == 0) return -MATE + ply;
        order(list, ply);
        for (Move m : list) {
            if (!in_check && !is_capture(m) && !(is_promo(m) && promo_type(m) == QUEEN)) continue;
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

    int negamax(int depth, int alpha, int beta, int ply) {
        pv_len[ply] = 0;
        if (ply > 0 && (pos.halfmove >= 100 || pos.is_repetition())) return 0;
        bool in_check = pos.in_check();
        if (in_check) depth++;  // check extension: don't let a check push mate past the horizon
        if (depth <= 0) return quiescence(alpha, beta, ply);
        nodes++;
        if (should_stop()) return 0;
        if (ply >= MAX_PLY - 1) return evaluate(pos);

        MoveList list;
        generate_legal(pos, list);
        if (list.size == 0) return in_check ? -MATE + ply : 0;  // checkmate or stalemate
        order(list, ply);

        int best = -MATE;
        for (Move m : list) {
            Undo u;
            pos.make_move(m, u);
            int score = -negamax(depth - 1, -beta, -alpha, ply + 1);
            pos.unmake_move(m, u);
            if (aborted) return 0;

            if (score > best) best = score;
            if (score > alpha) {
                alpha = score;
                pv[ply][0] = m;
                for (int i = 0; i < pv_len[ply + 1]; i++) pv[ply][i + 1] = pv[ply + 1][i];
                pv_len[ply] = pv_len[ply + 1] + 1;
            }
            if (alpha >= beta) {
                if (!is_capture(m) && m != killers[ply][0]) {
                    killers[ply][1] = killers[ply][0];
                    killers[ply][0] = m;
                }
                break;
            }
        }
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
    Searcher s{pos, stop, Clock::now(), limits.time_ms};
    SearchResult result;

    for (int depth = 1; depth <= limits.depth; depth++) {
        s.can_abort = depth > 1;
        int score = s.negamax(depth, -MATE - 1, MATE + 1, 0);
        if (s.aborted) break;  // partial iteration: keep the last complete one
        if (s.pv_len[0] == 0) break;  // no legal moves
        result = {s.pv[0][0], score, depth, s.nodes};
        s.root_best = result.best;

        int64_t ms = s.elapsed_ms();
        std::ostringstream info;
        info << "info depth " << depth << " score " << score_string(score) << " nodes " << s.nodes
             << " nps " << s.nodes * 1000 / (ms > 0 ? ms : 1) << " time " << ms << " pv";
        for (int i = 0; i < s.pv_len[0]; i++) info << ' ' << move_to_uci(s.pv[0][i]);
        out << info.str() << std::endl;

        if (score > MATE - MAX_PLY || score < -MATE + MAX_PLY) break;  // mate found; deeper won't change it
        // The next depth takes several times longer than this one; don't start what can't finish.
        if (limits.time_ms >= 0 && ms * 2 > limits.time_ms) break;
    }
    result.nodes = s.nodes;
    return result;
}
