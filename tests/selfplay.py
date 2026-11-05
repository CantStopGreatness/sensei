"""Play Sensei over UCI and check every move it makes with python-chess.

Usage:
  pip install chess
  python3 tests/selfplay.py --games 100                 # Sensei vs itself
  python3 tests/selfplay.py --games 50 --vs-random      # Sensei vs a random mover; must win every game
Fails on any illegal move, a bestmove that disagrees with game-over state, or a crash.
"""
import argparse
import random
import subprocess
import sys

import chess

ap = argparse.ArgumentParser()
ap.add_argument("--games", type=int, default=100)
ap.add_argument("--engine", default="build/sensei")
ap.add_argument("--go", default="go movetime 20", help="UCI go command Sensei gets each move")
ap.add_argument("--vs-random", action="store_true")
args = ap.parse_args()

engine = subprocess.Popen([args.engine], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)


def send(cmd):
    engine.stdin.write(cmd + "\n")
    engine.stdin.flush()


def wait_for(prefix):
    while True:
        line = engine.stdout.readline()
        if not line:
            sys.exit(f"engine exited while waiting for {prefix!r}")
        if line.startswith(prefix):
            return line.split()


send("uci")
wait_for("uciok")
results = {}
plies = 0
for g in range(args.games):
    board = chess.Board()
    sensei_color = chess.WHITE if g % 2 == 0 else chess.BLACK  # alternate colors vs random
    send("ucinewgame")
    while not board.is_game_over(claim_draw=True):
        if args.vs_random and board.turn != sensei_color:
            board.push(random.choice(list(board.legal_moves)))
            continue
        moves = " ".join(m.uci() for m in board.move_stack)
        send("position startpos" + (" moves " + moves if moves else ""))
        send(args.go)
        best = wait_for("bestmove")[1]
        assert chess.Move.from_uci(best) in board.legal_moves, f"game {g}: illegal {best} after {moves}"
        board.push_uci(best)
        plies += 1
    # Game over by python-chess's rules: engine must agree there's nothing to play when mated/stalemated.
    if board.is_checkmate() or board.is_stalemate():
        moves = " ".join(m.uci() for m in board.move_stack)
        send("position startpos moves " + moves)
        send("go depth 1")
        assert wait_for("bestmove")[1] == "0000", f"game {g}: engine moved in a finished game"
    result = board.result(claim_draw=True)
    if args.vs_random:
        won = (result == "1-0") == (sensei_color == chess.WHITE) and result != "1/2-1/2"
        result = "Sensei won" if won else f"NOT WON ({result}): {board.fen()}"
    results[result] = results.get(result, 0) + 1

send("quit")
engine.wait()
print(f"{args.games} games, {plies} Sensei moves checked, all legal. Results: {results}")
if args.vs_random and results.get("Sensei won", 0) != args.games:
    sys.exit(1)
