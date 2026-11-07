"""Engine-vs-engine match with Elo estimate and SPRT, using python-chess.

Stand-in for fastchess/cutechess-cli. Each random 8-ply opening is played twice, colors
swapped, so opening luck cancels out. Clocks are enforced: running out of time loses.

Usage:
  pip install chess
  python3 tests/match.py NEW_ENGINE OLD_ENGINE [--games 200] [--tc 5+0.05] [--concurrency 6]
"""
import argparse
import math
import random
import threading
import time
from concurrent.futures import ThreadPoolExecutor

import chess
import chess.engine

ap = argparse.ArgumentParser()
ap.add_argument("new")
ap.add_argument("old")
ap.add_argument("--games", type=int, default=200, help="rounded up to an even number")
ap.add_argument("--tc", default="5+0.05", help="seconds+increment per side")
ap.add_argument("--concurrency", type=int, default=6)
ap.add_argument("--new-option", action="append", default=[], metavar="NAME=VALUE",
                help="UCI option for NEW, e.g. Threads=4; repeatable")
ap.add_argument("--old-option", action="append", default=[], metavar="NAME=VALUE", help="same, for OLD")
ap.add_argument("--elo0", type=float, default=0)
ap.add_argument("--elo1", type=float, default=10)
args = ap.parse_args()
base, inc = (float(x) for x in args.tc.split("+"))


def random_opening(rng):
    board = chess.Board()
    while True:
        board.reset()
        for _ in range(8):
            board.push(rng.choice(list(board.legal_moves)))
            if board.is_game_over():
                break
        if not board.is_game_over():
            return board.move_stack[:]


def parse_options(pairs):
    return dict(p.split("=", 1) for p in pairs)


OPTIONS = {"new": parse_options(args.new_option), "old": parse_options(args.old_option)}


def play_game(paths, opening, options):
    """paths[0] plays White. Returns 1, 0.5 or 0 from White's point of view, and how it ended."""
    engines = [chess.engine.SimpleEngine.popen_uci(p) for p in paths]
    try:
        for engine, opts in zip(engines, options):
            if opts:
                engine.configure(opts)
        board = chess.Board()
        for m in opening:
            board.push(m)
        clock = [base, base]
        game_id = object()
        while not board.is_game_over(claim_draw=True) and board.ply() < 400:
            side = 0 if board.turn == chess.WHITE else 1
            limit = chess.engine.Limit(white_clock=clock[0], black_clock=clock[1], white_inc=inc, black_inc=inc)
            t0 = time.monotonic()
            move = engines[side].play(board, limit, game=game_id).move
            clock[side] -= time.monotonic() - t0
            if clock[side] < 0:
                return (0.0 if side == 0 else 1.0), "time forfeit"
            clock[side] += inc
            if move not in board.legal_moves:
                return (0.0 if side == 0 else 1.0), f"illegal move {move}"
            board.push(move)
        result = board.result(claim_draw=True)
        if result == "*":
            return 0.5, "400-ply limit"
        return {"1-0": 1.0, "0-1": 0.0, "1/2-1/2": 0.5}[result], board.outcome(claim_draw=True).termination.name
    finally:
        for e in engines:
            e.quit()


lock = threading.Lock()
wins = draws = losses = 0
endings = {}


def play_pair(i):
    global wins, draws, losses
    opening = random_opening(random.Random(i))
    for new_is_white in (True, False):
        paths = [args.new, args.old] if new_is_white else [args.old, args.new]
        options = [OPTIONS["new"], OPTIONS["old"]] if new_is_white else [OPTIONS["old"], OPTIONS["new"]]
        white_score, how = play_game(paths, opening, options)
        score = white_score if new_is_white else 1 - white_score
        with lock:
            if score == 1:
                wins += 1
            elif score == 0:
                losses += 1
            else:
                draws += 1
            endings[how] = endings.get(how, 0) + 1
            n = wins + draws + losses
            print(f"\r{n}/{pairs * 2} games  +{wins} ={draws} -{losses}", end="", flush=True)


def elo(score):
    score = min(max(score, 1e-6), 1 - 1e-6)
    return -400 * math.log10(1 / score - 1)


pairs = (args.games + 1) // 2
with ThreadPoolExecutor(args.concurrency) as pool:
    list(pool.map(play_pair, range(pairs)))

n = wins + draws + losses
mean = (wins + 0.5 * draws) / n
var = (wins * (1 - mean) ** 2 + draws * (0.5 - mean) ** 2 + losses * mean ** 2) / n
err = 1.96 * math.sqrt(var / n)
print(f"\n\n{args.new} vs {args.old}, {n} games at {args.tc}")
print(f"Score {mean:.3f}  Elo {elo(mean):+.1f} +/- {(elo(mean + err) - elo(mean - err)) / 2:.1f} (95%)")
print(f"Endings: {endings}")

# SPRT, normal approximation (as fishtest's simple form). alpha = beta = 0.05.
s0, s1 = (1 / (1 + 10 ** (-e / 400)) for e in (args.elo0, args.elo1))
llr = n * (s1 - s0) * (2 * mean - s0 - s1) / (2 * var) if var > 0 else 0.0
lower, upper = math.log(0.05 / 0.95), math.log(0.95 / 0.05)
verdict = "H1 accepted (gain)" if llr >= upper else "H0 accepted (no gain)" if llr <= lower else "inconclusive"
print(f"SPRT [{args.elo0}, {args.elo1}]: LLR {llr:.2f} (bounds {lower:.2f}, {upper:.2f}) -> {verdict}")
