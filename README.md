# Sensei

Sensei is a UCI chess engine in C++17 with bitboard move generation. Work in progress: board, FEN and perft-verified legal move generation are done; UCI and search are next.

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Use

```bash
./build/sensei print [fen]           # pretty-print a position
./build/sensei perft <depth> [fen]   # count leaf nodes
./build/sensei divide <depth> [fen]  # perft split by root move
```

## Tests

```bash
./build/perft_tests          # FEN round-trip + perft on 7 standard positions (~1s)
./build/perft_tests --deep   # one ply deeper on each, up to 194M nodes (~10s)
```

Perft numbers come from the [Chess Programming Wiki](https://www.chessprogramming.org/Perft_Results). All match, including start position depth 6 (119,060,324) and Kiwipete depth 5 (193,690,690).
