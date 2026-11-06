# Sensei

Sensei is a UCI chess engine in C++17 with bitboard move generation. It uses alpha-beta search with iterative deepening and quiescence, plus a material and piece-square-table evaluation and a transposition table (UCI option `Hash`, in MB). Lazy SMP spreads the search over several threads (UCI option `Threads`).

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Use

Run `./build/sensei` with no arguments for UCI mode, and point Cute Chess, Arena or lichess-bot at that binary. It supports `uci`, `isready`, `ucinewgame`, `position startpos|fen ... [moves ...]`, `go` (including `go ponder`), `ponderhit`, `stop`, `quit`, plus `d` to print the board.

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
python3 tests/ponder_test.py                       # go ponder / ponderhit / stop over UCI, with real timing
python3 tests/match.py NEW OLD --games 200 --tc 5+0.05  # match two builds: Elo +/- error and SPRT
python3 tests/match.py NEW OLD --new-option Threads=4  # set UCI options per side (repeatable; also --old-option)
```

CI (`.github/workflows/ci.yml`) runs the perft and search tests and 20 games against a random mover on Linux and macOS for every pull request and every push to `main`.

### Tuning the evaluation

All evaluation weights live in `EvalParams` (`src/eval.h`). Texel tuning fits them to game results:

```bash
python3 tools/gen_data.py --games 6000 --out data/positions.txt  # quiet positions from self-play
./build/tune data/positions.txt tuned.txt                         # fit the weights; prints the error as it goes
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
5. From then on, start it with `python3 lichess-bot.py`, or on macOS let launchd run it (below).

The config uses the Lichess masters database as an opening book and Lichess's online tablebases for endgames with 7 or fewer pieces.

### Keeping it running (macOS)

lichess-bot exits if a restart fails, for example when the network drops, and stays down. `deploy/launchd/` makes launchd start it at login and restart it whenever it exits, at most once a minute so Lichess doesn't rate-limit the reconnects. The token lives in a file only you can read, not in your shell config:

```bash
mkdir -p ~/.config/lichess-bot
read -rs TOKEN && (umask 077; printf '%s' "$TOKEN" > ~/.config/lichess-bot/token) && unset TOKEN  # paste the token, then Enter
cp ~/sensei-release/deploy/launchd/run.sh ~/.config/lichess-bot/ && chmod 700 ~/.config/lichess-bot/run.sh
sed "s#__HOME__#$HOME#g" ~/sensei-release/deploy/launchd/com.aryanthakur.sensei-lichess-bot.plist \
  > ~/Library/LaunchAgents/com.aryanthakur.sensei-lichess-bot.plist
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.aryanthakur.sensei-lichess-bot.plist
```

Logs go to `~/lichess-bot/bot.log`. Restart with `launchctl kickstart -k gui/$(id -u)/com.aryanthakur.sensei-lichess-bot`, and stop with `launchctl bootout gui/$(id -u)/com.aryanthakur.sensei-lichess-bot`. Don't also start lichess-bot by hand: two copies on one token get rate-limited. The bot is offline while the Mac sleeps and reconnects when it wakes.

### Deploying a new version

After merging to `main`: `cd ~/sensei-release && git pull && cmake --build build --target sensei`, then restart lichess-bot (the `kickstart` command above).

## Results

| Version | Feature added | Elo vs. previous (±error) |
|---|---|---|
| v0.1 | Random mover | — |
| v0.2 | Material + PST eval, alpha-beta, quiescence, MVV-LVA + killers | Beats random 100/100 |
| v0.3 | Transposition table | +149 ± 47 (200 games, 5s+0.05s, SPRT [0,10] passed) |
| v0.4 | Null-move pruning | +78 ± 26 (600 games, 5s+0.05s, SPRT [0,10] passed) |
| v0.5 | History heuristic, late move reductions, PVS | +137 ± 33 (400 games, 5s+0.05s, SPRT [0,10] passed) |
| v0.6 | Tapered eval, passed/doubled/isolated pawns, bishop pair, rooks on open files | +180 ± 36 (400 games, 5s+0.05s, SPRT [0,10] passed) |
| v0.7 | Soft/hard time limits, best-move stability | +68 ± 25 (600 games, 5s+0.05s, SPRT [0,10] passed) |
| v0.8 | SEE, reverse futility pruning, futility pruning | +152 ± 33 (400 games, 5s+0.05s, SPRT [0,10] passed) |
| v0.9 | Mobility, king safety (pawn shelter, king attack) | +69 ± 26 (600 games, 5s+0.05s, SPRT [0,10] passed) |
| v0.10 | Captures-only quiescence generation, aspiration windows | +70 ± 29 (400 games, 5s+0.05s, SPRT [0,10] passed) |

v0.10 against v0.3 directly: +521 ± 109 (200 games, 5s+0.05s; 95.3% score). Gaps this lopsided compress, so the per-version gains don't simply add up.
