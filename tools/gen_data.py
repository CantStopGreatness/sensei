"""Generate Texel tuning data: quiet positions from Sensei self-play, labelled with the result.

  python3 tools/gen_data.py --games 6000 --out data/positions.txt

Each game starts from a random 8-ply opening and is played at a fixed depth. From ply 16 on,
a position is kept when the side to move isn't in check, the engine's move isn't a capture or
promotion, and the score isn't a mate: positions where the static eval should be right. Output
lines are "FEN;result", result from White's side: 1.0, 0.5 or 0.0.
"""
import argparse
import random
import threading
from concurrent.futures import ThreadPoolExecutor

import chess
import chess.engine

ap = argparse.ArgumentParser()
ap.add_argument("--engine", default="build/sensei")
ap.add_argument("--games", type=int, default=1000)
ap.add_argument("--depth", type=int, default=7)
ap.add_argument("--concurrency", type=int, default=8)
ap.add_argument("--out", default="data/positions.txt")
ap.add_argument("--seed", type=int, default=1)
args = ap.parse_args()

RESULT = {"1-0": "1.0", "0-1": "0.0", "1/2-1/2": "0.5"}
lock = threading.Lock()
done = kept = 0
out = open(args.out, "a")


def play(game_no):
    global done, kept
    rng = random.Random(args.seed * 1_000_003 + game_no)
    engine = chess.engine.SimpleEngine.popen_uci(args.engine)
    try:
        board = chess.Board()
        for _ in range(8):
            if board.is_game_over():
                return
            board.push(rng.choice(list(board.legal_moves)))
        fens = []
        while not board.is_game_over(claim_draw=True) and board.ply() < 300:
            info = engine.analyse(board, chess.engine.Limit(depth=args.depth))
            move = info["pv"][0]
            score = info["score"].relative
            if (board.ply() >= 16 and not board.is_check() and not board.is_capture(move)
                    and not move.promotion and not score.is_mate()):
                fens.append(board.fen())
            board.push(move)
        result = RESULT.get(board.result(claim_draw=True), "0.5")  # 300-ply cap counts as a draw
        with lock:
            out.writelines(f"{f};{result}\n" for f in fens)
            out.flush()
            done += 1
            kept += len(fens)
            print(f"\r{done}/{args.games} games, {kept} positions", end="", flush=True)
    finally:
        engine.quit()


with ThreadPoolExecutor(args.concurrency) as pool:
    list(pool.map(play, range(args.games)))
print()
