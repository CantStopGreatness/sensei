"""Pondering over UCI, with real timing: no bestmove while pondering, the clock starts at
ponderhit, "stop" ends a ponder search, and bestmove carries a ponder move.

  python3 tests/ponder_test.py [--engine build/sensei]
"""
import argparse
import queue
import subprocess
import sys
import threading
import time

ap = argparse.ArgumentParser()
ap.add_argument("--engine", default="build/sensei")
args = ap.parse_args()

engine = subprocess.Popen([args.engine], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
lines = queue.Queue()
threading.Thread(target=lambda: [lines.put(l.strip()) for l in engine.stdout], daemon=True).start()
failures = 0


def send(cmd):
    engine.stdin.write(cmd + "\n")
    engine.stdin.flush()


def bestmove_within(seconds):
    """The bestmove line if one arrives within `seconds`, else None."""
    deadline = time.monotonic() + seconds
    while (left := deadline - time.monotonic()) > 0:
        try:
            line = lines.get(timeout=left)
        except queue.Empty:
            return None
        if line.startswith("bestmove"):
            return line
    return None


def check(ok, what):
    global failures
    print(("  ok    " if ok else "  FAIL  ") + what)
    failures += not ok


send("uci")
saw_option = False
while (line := lines.get(timeout=5)) != "uciok":
    saw_option |= line.startswith("option name Ponder")
check(saw_option, "advertises the Ponder option")

CLOCK = "wtime 6000 btime 6000"  # hard limit 6000/3 = 2s after ponderhit

send("position startpos")
send("go ponder " + CLOCK)
check(bestmove_within(3.0) is None, "no bestmove while pondering, even past the clock's hard limit")
t0 = time.monotonic()
send("ponderhit")
line = bestmove_within(4.0)
took = time.monotonic() - t0
check(line is not None and took < 2.5, f"bestmove within the time limit after ponderhit ({took:.2f}s): {line}")
check(line is not None and " ponder " in line, "bestmove names a ponder move")

# Mate in 1: the search finishes at once, but must still wait for stop/ponderhit.
send("position fen 6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1")
send("go ponder " + CLOCK)
check(bestmove_within(1.0) is None, "a finished ponder search still waits")
send("stop")
line = bestmove_within(1.0)
check(line is not None and line.split()[1] == "d1d8", f"stop ends a ponder search with the best move: {line}")

send("position fen 6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1")
send("go ponder " + CLOCK)
time.sleep(0.2)
send("ponderhit")
line = bestmove_within(1.0)
check(line is not None and line.split()[1] == "d1d8", f"ponderhit after the search finished answers at once: {line}")

send("position startpos")
send("go movetime 300")
line = bestmove_within(2.0)
check(line is not None and " ponder " in line, f"a normal search also names a ponder move: {line}")

send("quit")
engine.wait(timeout=5)
print("All tests passed" if not failures else f"{failures} FAILED")
sys.exit(1 if failures else 0)
