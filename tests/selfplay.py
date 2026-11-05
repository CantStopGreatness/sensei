"""Play Sensei against itself over UCI and check every move with python-chess.

Usage: pip install chess && python3 tests/selfplay.py [games] [path/to/sensei]
Fails on any illegal move, a bestmove that disagrees with game-over state, or a crash.
"""
import subprocess
import sys

import chess

games = int(sys.argv[1]) if len(sys.argv) > 1 else 100
binary = sys.argv[2] if len(sys.argv) > 2 else "build/sensei"

engine = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)


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
for g in range(games):
    board = chess.Board()
    send("ucinewgame")
    moves = []
    while True:
        send("position startpos" + (" moves " + " ".join(moves) if moves else ""))
        send("go wtime 1000 btime 1000")
        best = wait_for("bestmove")[1]
        legal = list(board.legal_moves)
        if not legal:  # mate or stalemate: engine must say so
            assert best == "0000", f"game {g}: no legal moves but engine played {best}"
            break
        assert chess.Move.from_uci(best) in legal, f"game {g}: illegal {best} after {' '.join(moves)}"
        board.push_uci(best)
        moves.append(best)
        if board.is_game_over(claim_draw=True):
            break
    plies += len(moves)
    result = board.result(claim_draw=True)
    results[result] = results.get(result, 0) + 1

send("quit")
engine.wait()
print(f"{games} games, {plies} moves checked, all legal. Results: {results}")
