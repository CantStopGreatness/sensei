# Sensei

Sensei is a UCI chess engine in C++17 with bitboard move generation. It uses alpha-beta search with iterative deepening and quiescence, plus a material and piece-square-table evaluation and a transposition table (UCI option `Hash`, in MB).

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Use

Run `./build/sensei` with no arguments for UCI mode, and point Cute Chess, Arena or lichess-bot at that binary. It supports `uci`, `isready`, `ucinewgame`, `position startpos|fen ... [moves ...]`, `go`, `stop`, `quit`, plus `d` to print the board.

```bash
./build/sensei print [fen]           # pretty-print a position
./build/sensei perft <depth> [fen]   # count leaf nodes
./build/sensei divide <depth> [fen]  # perft split by root move
```

## Tests

```bash
./build/perft_tests          # FEN round-trip + perft on 7 standard positions (~1s)
./build/perft_tests --deep   # one ply deeper on each, up to 194M nodes (~10s)
./build/search_tests         # hashing, repetition, mate-in-1/2 puzzles, hanging pieces, depth-6 speed
pip install chess
python3 tests/selfplay.py --games 100              # self-play over UCI, every move checked by python-chess
python3 tests/selfplay.py --games 100 --vs-random  # must beat a random mover every game
python3 tests/match.py NEW OLD --games 200 --tc 5+0.05  # match two builds: Elo +/- error and SPRT
```

Perft numbers come from the [Chess Programming Wiki](https://www.chessprogramming.org/Perft_Results). All match, including start position depth 6 (119,060,324) and Kiwipete depth 5 (193,690,690).

## Results

| Version | Feature added | Elo vs. previous (±error) |
|---|---|---|
| v0.1 | Random mover | — |
| v0.2 | Material + PST eval, alpha-beta, quiescence, MVV-LVA + killers | Beats random 100/100 |
| v0.3 | Transposition table | +149 ± 47 (200 games, 5s+0.05s, SPRT [0,10] passed) |
