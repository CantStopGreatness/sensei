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

To play it in a browser: `python3 tools/play.py`, then open http://localhost:8765.

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

## Running on Lichess

Sensei plays through [lichess-bot](https://github.com/lichess-bot-devs/lichess-bot) (Python 3.11+), using `deploy/lichess-bot.yml`.

1. Create a **new** Lichess account. It must have no games played, or it can't become a bot.
2. Create a token with the `bot:play` scope: <https://lichess.org/account/oauth/token/create?scopes%5B%5D=bot:play&description=lichess-bot>. Lichess shows it only once.
3. Make a release clone of Sensei just for the bot, so the bot only ever plays `main`, never a development build. Then clone lichess-bot next to it and install it:
   ```bash
   cd ~ && git clone https://github.com/CantStopGreatness/sensei.git sensei-release
   cmake -S sensei-release -B sensei-release/build && cmake --build sensei-release/build --target sensei
   git clone https://github.com/lichess-bot-devs/lichess-bot.git && cd lichess-bot
   python3 -m venv venv && . venv/bin/activate && python3 -m pip install -r requirements.txt
   ln -s ../sensei-release/deploy/lichess-bot.yml config.yml
   export LICHESS_BOT_TOKEN=lip_...
   ```
4. Upgrade the account to a bot. **This can't be undone**, and the bot starts playing right after: `python3 lichess-bot.py -u`
5. From then on, start it with `python3 lichess-bot.py`.

The config uses the Lichess masters database as an opening book and Lichess's online tablebases for endgames with 7 or fewer pieces.

To put a new version on Lichess after merging to `main`: `cd ~/sensei-release && git pull && cmake --build build --target sensei`, then restart lichess-bot.

## Results

| Version | Feature added | Elo vs. previous (±error) |
|---|---|---|
| v0.1 | Random mover | — |
| v0.2 | Material + PST eval, alpha-beta, quiescence, MVV-LVA + killers | Beats random 100/100 |
| v0.3 | Transposition table | +149 ± 47 (200 games, 5s+0.05s, SPRT [0,10] passed) |
| v0.4 | Null-move pruning | +78 ± 26 (600 games, 5s+0.05s, SPRT [0,10] passed) |
| v0.5 | History heuristic, late move reductions, PVS | +137 ± 33 (400 games, 5s+0.05s, SPRT [0,10] passed) |
