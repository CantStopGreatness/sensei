# Sensei

Sensei is a UCI chess engine in C++17 with bitboard move generation. Work in progress: board, FEN, perft-verified move generation and UCI are done. It currently plays random legal moves; search is next.

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
pip install chess && python3 tests/selfplay.py 500   # self-play over UCI, every move checked by python-chess
```

Perft numbers come from the [Chess Programming Wiki](https://www.chessprogramming.org/Perft_Results). All match, including start position depth 6 (119,060,324) and Kiwipete depth 5 (193,690,690).
