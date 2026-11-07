# Opening Coach Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** BigElephant67 coaches beginners through the opening in Lichess chat. Common human moves, main lines, traps, gambit refutations, leaving-book notes and mistake explanations go to chat, and the bot punishes mistakes on the board. After the opening it plays at a player-chosen strength.

**Architecture:**
- **Engine:** the C++ engine gains UCI strength limiting (`UCI_LimitStrength`/`UCI_Elo`).
- **Coach package:** a new Python package `coach/` in the sensei repo holds the coaching logic: explorer client, opening names, traps, move judge and per-game session.
- **lichess-bot hook:** a lichess-bot *homemade engine* class `SenseiCoach` wraps the Sensei UCI binary with python-chess. lichess-bot creates one per game. `SenseiCoach` decides whether the game is a coaching game, and reads and posts chat through the Lichess bot chat API.
- **Unmodified upstream:** lichess-bot's files stay untouched. Its `homemade.py` becomes a symlink to `deploy/homemade.py`, the same way `config.yml` is a symlink today.

**Tech Stack:**
- C++17 engine.
- Python 3 with python-chess and requests (both already in `~/lichess-bot/venv`).
- Tests: stdlib `unittest` (pytest isn't installed; don't add it).
- Data: Lichess opening explorer API, Lichess tablebase API, and the lichess-org/chess-openings TSVs (CC0).

**Spec:** `docs/superpowers/specs/2026-11-07-opening-coach-design.md`

## Global Constraints

- **When coaching is on:** only if the opponent is not a BOT, the game is casual, the time control is rapid or slower (estimated `initial + 40*increment >= 600` s), the game started from the normal position, and it is standard chess. Every other game plays exactly as today: full strength, masters book (`sort: winrate`, `min_games: 25`), online EGTB at 7 pieces or fewer, and pondering.
- **Chat:** every message is at most 140 characters, plain language, and never shows raw evals ("-1.3", "cp"). At most one coaching message per beginner move. The greeting, command replies and the end-of-opening summary are extra. When unsure, say nothing.
- **Opening phase:** lasts while the position is in either database. It ends at the first Sensei turn where the position has fewer than 5 masters games and fewer than 50 Lichess games, or once the fullmove number exceeds 15.
- **Levels:** `!level <400..2400>` or `!level max`. Default 1200. Per game only, nothing persisted. `max` means `UCI_LimitStrength false`.
- **Mistakes:** Sensei punishes them at full strength regardless of level, for its next move.
- **Robustness:** a failing explorer, tablebase or chat call must never stall or lose a game. Use a 2 s timeout per call. On failure, play the engine move and post nothing.
- **Secrets:** the token is read from env `LICHESS_BOT_TOKEN`. Never log, print or commit it.
- **Commits:** authored as Aryan Thakur <aryan.thakur1508@gmail.com>, ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **Paths:**
  - cmake: `/Users/aryanthakur/Library/Python/3.9/lib/python/site-packages/cmake/data/bin/cmake` (ctest sits next to it).
  - Python: `~/lichess-bot/venv/bin/python` (system python3 lacks python-chess).
- **Work location:** branch `opening-coach`, already created with the spec committed.

## Review Focus

1. **Explorer down, slow or rate-limited (429):** Sensei still moves in time with an engine move and posts nothing. Test in Task 5 (`test_explorer_failure_is_silent`).
2. **Garbage commands:** `!level abc`, `!level 99999`, `!LEVEL 800`, `!opening xyzzy`. Each gets one short help reply. Out-of-range levels are clamped to 400–2400, and commands are case-insensitive. Test in Task 5 (`test_command_parsing`).
3. **Takebacks and restarts:** a takeback (lichess-bot accepts them) or a restart mid-game (launchd) means the board's history can shrink or arrive already long. Session state is derived from the board each call, so it must never crash, re-greet, or comment twice on the same ply. Test in Task 5 (`test_takeback_and_resume`).
4. **"From Position" games:** a standard game that starts from a custom FEN must not be coached, since there is no opening theory. Test in Task 6 (`test_coaching_gate`).
5. **Long SAN or names:** an awkward name or move (e.g. `exd8=Q+`, "Ruy Lopez: Morphy Defense, Columbus Variation") must not push a message past 140 characters. `fit()` trims names first. Test in Task 4 (`test_messages_fit`).

---

### Task 1: Strength limiting in the engine

**Files:**
- Modify: `src/search.h` (SearchLimits)
- Modify: `src/search.cpp` (`search()`)
- Modify: `src/uci.cpp` (options, setoption, building limits)
- Test: `tests/search_tests.cpp`

**Interfaces:**
- Produces:
  - `SearchLimits::elo` (`int elo = 0;`, 0 = full strength).
  - `SearchLimits::seed` (`uint32_t seed = 0;`, 0 = seed from `std::random_device`).
  - UCI options:
    - `option name UCI_LimitStrength type check default false`
    - `option name UCI_Elo type spin default 1200 min 400 max 2400`
  - When `UCI_LimitStrength` is true, `go` uses `elo = UCI_Elo`.

**Algorithm** (the shape is decided here; Task 7 adjusts the constants):
1. When `elo > 0`, cap the search depth at `1 + (elo - 400) / 200`, so 400 gives depth 1 and 2400 gives depth 11. Time limits still apply, and helper threads are not used.
2. After the capped search, score every legal root move: make it, then call `-negamax(depth_cap - 1)` with a full window.
3. Choose move *i* with probability proportional to `exp((s_i - s_best) / T)`, where `T = max(10, (2400 - elo) / 8)` centipawns, using `std::mt19937` seeded from `seed`.
4. A move that mates (its mate score is within MAX_PLY of MATE) is always chosen, at any level.
5. The ponder move is taken from the chosen move's line, or is null.

- [ ] **Step 1: Write the failing tests** in `tests/search_tests.cpp`, using the existing `run(...)` helper style:
  - `limited_strength_varies_at_400`:
    - Position: start position.
    - Run: `elo = 400`, seeds 1..40, `time_ms = 200`.
    - Assert: at least 8 distinct best moves are returned, and every one is legal.
  - `limited_strength_takes_free_queen_at_2000`:
    - Position: `rnb1kbnr/pppp1ppp/8/4p1q1/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 1`, where `Bxg5` wins the queen.
    - Run: `elo = 2000`, seeds 1..20.
    - Assert: `c1g5` is returned in at least 18 of 20.
  - `limited_strength_finds_mate_in_1_at_400`:
    - Position: `6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1`.
    - Run: `elo = 400`, seeds 1..20.
    - Assert: all return `d1d8`.
  - `elo_zero_is_full_strength`:
    - Run: `elo = 0`, depth 6, start position.
    - Assert: same best move and node count as the existing unlimited call.
- [ ] **Step 2: Build and run the tests to verify they fail**
  - Run: `cmake --build build -j && ./build/search_tests`
  - Expected: compile error on `elo`/`seed`.
- [ ] **Step 3: Implement** the algorithm above in `search()`, plus the two UCI options. Parse `setoption name UCI_LimitStrength value true|false` and `UCI_Elo value <n>`, clamping to 400..2400.
- [ ] **Step 4: Run all tests**
  - Run: `cd build && ctest && cd .. && ~/lichess-bot/venv/bin/python tests/ponder_test.py`
  - Expected: `100% tests passed` and `All tests passed`.
- [ ] **Step 5: Manual UCI check**
  - Run: `printf 'uci\nsetoption name UCI_LimitStrength value true\nsetoption name UCI_Elo value 400\nposition startpos\ngo movetime 300\nquit\n' | ./build/sensei`
  - Expected: both options are listed, and a `bestmove` line is printed.
- [ ] **Step 6: Commit** `feat(engine): UCI_LimitStrength / UCI_Elo strength limiting`

### Task 2: Explorer client and opening names

**Files:**
- Create: `coach/__init__.py` (empty)
- Create: `coach/explorer.py`
- Create: `coach/openings.py`
- Create: `coach/data/openings.tsv`: concatenate `a.tsv` … `e.tsv` from https://github.com/lichess-org/chess-openings (CC0; keep its `eco	name	pgn` header once), plus a one-line source note in `coach/data/README`.
- Test: `tests/coach/__init__.py` (empty)
- Test: `tests/coach/test_explorer.py`
- Test: `tests/coach/test_openings.py`

**Interfaces:**
- Produces in `coach/explorer.py`:
  - `@dataclass MoveStat: uci: str; san: str; games: int; white: int; draws: int; black: int`
  - `class Explorer:` `__init__(self, session: requests.Session | None = None, timeout: float = 2.0)`
    - `masters(board: chess.Board) -> list[MoveStat] | None`
    - `lichess(board: chess.Board, rating: int) -> list[MoveStat] | None`
    - `tablebase_best(board: chess.Board) -> chess.Move | None`
    - Behaviour:
      - Each list is sorted by `games`, descending.
      - `None` on any network error, timeout or non-200 response; it never raises.
      - Results are cached per (endpoint, `board.epd()`, rating) for the life of the object.
  - Endpoints:
    - masters: `https://explorer.lichess.ovh/masters?fen=…&moves=12`
    - lichess: `https://explorer.lichess.ovh/lichess?variant=standard&speeds=rapid,classical&ratings=<bucket>&fen=…&moves=12`. The bucket is the two Lichess rating buckets nearest `rating`, chosen from 400, 1000, 1200, 1400, 1600, 1800, 2000, 2200, 2500.
    - tablebase: `https://tablebase.lichess.ovh/standard?fen=…`, which returns the first move in `moves` (they come sorted best-first).
    - Send the bot token as a Bearer header when env `LICHESS_BOT_TOKEN` is set. The explorer needs it.
- Produces in `coach/openings.py`:
  - `@dataclass Opening: eco: str; name: str; moves: list[chess.Move]`
  - `name_for(board: chess.Board) -> str | None`: the name of the deepest TSV line whose final position equals `board.epd()`, or `None`.
  - `find(query: str) -> list[Opening]`: every line whose lowercased name contains every word of the lowercased query. Matches are ordered by name length, then line length. `[]` means not found.
  - `family(op: Opening) -> str`: the name before the first `:`.

- [ ] **Step 1: Write the failing tests**
  - `test_explorer.py`, using a fake `requests.Session` subclass with a canned `get`; no network:
    - `test_parses_and_sorts`: canned masters JSON with `e2e4` 100 games and `d2d4` 300 → `[d2d4, e2e4]`, with san `d4`/`e4`.
    - `test_error_returns_none`: fake raises `requests.Timeout`, then returns status 429 → both give `None`.
    - `test_cache`: two calls on the same board → the fake is called once.
    - `test_tablebase_best`: canned `{"moves":[{"uci":"a7a8q"}]}` → `chess.Move.from_uci("a7a8q")`.
  - `test_openings.py`:
    - `name_for` after `1.e4 e5 2.Nf3 Nc6 3.Bc4` returns `"Italian Game"`.
    - `find("italian")` is non-empty, and every name contains "Italian".
    - `find("ruy lopez")[0].moves[:5]` equals `e2e4 e7e5 g1f3 b8c6 f1b5`.
    - `find("xyzzy") == []`.
    - `family()` of a "Sicilian Defense: Najdorf Variation" line is `"Sicilian Defense"`.
- [ ] **Step 2: Run the tests to verify they fail**
  - Run: `~/lichess-bot/venv/bin/python -m unittest discover -s tests/coach -t . -v`
  - Expected: `ModuleNotFoundError: coach.explorer`.
- [ ] **Step 3: Implement** `explorer.py` and `openings.py`. Download the TSVs with curl. Parse the TSV once at import time into an epd→name dict and a list of `Opening`.
- [ ] **Step 4: Run the tests to verify they pass**
  - Same command. Expected: OK.
- [ ] **Step 5: One live smoke run, not in the suite**
  - Run: `~/lichess-bot/venv/bin/python -c "import chess;from coach.explorer import Explorer;e=Explorer();print(e.masters(chess.Board())[:2], e.lichess(chess.Board(),1200)[:2])"`
  - Expected: two MoveStats each. If you get a 401, export the token first.
- [ ] **Step 6: Commit** `feat(coach): explorer client and opening names`

### Task 3: Traps

**Files:**
- Create: `coach/traps.py`
- Create: `coach/data/traps.json`
- Test: `tests/coach/test_traps.py`

**Interfaces:**
- Produces:
  - `@dataclass Trap: name: str; setup: list[str]; bait: str; punish: str; note: str`
    - `setup`: SAN moves from the start position to the position where the victim is to move.
    - `bait`: the victim's tempting losing move, in SAN.
    - `punish`: the refuting reply, in SAN.
    - `note`: at most 60 characters, plain language, saying why it loses.
  - `TRAPS: list[Trap]`, loaded from json.
  - `live_trap(board: chess.Board) -> Trap | None`: returns the trap whose setup position equals `board.epd()`.
- Content:
  - 30–50 well-known opening traps across common beginner openings (Italian, Ruy Lopez, Scotch, Sicilian, French, Caro-Kann, Queen's Gambit, London, Scandinavian, Petrov, King's Gambit, Englund, Budapest, Stafford, and so on).
  - Some must have White as the victim and some Black.
  - Examples: Légal's mate; Fishing Pole; Blackburne Shilling; Englund Gambit trap; Lasker trap (Albin); Elephant trap (QGD); Noah's Ark (Ruy Lopez); Siberian trap (Smith-Morra); Stafford Gambit Nxe5 trap; Fried Liver setup.
  - Ship only traps that pass the test below.

- [ ] **Step 1: Write the failing tests** in `tests/coach/test_traps.py`:
  - `test_count`: `30 <= len(TRAPS) <= 50`, and names are unique.
  - `test_legal`: for each trap, every setup SAN, the bait and the punish move are legal in sequence.
  - `test_both_sides`: at least 5 traps have a White victim and at least 5 a Black victim.
  - `test_notes_short`: every `len(note) <= 60`.
  - `test_bait_really_loses`: the guard against confidently wrong advice.
    - Use the real engine `build/sensei` through `chess.engine.SimpleEngine` at depth 12, scoring each line from the victim's side.
    - Assert: the score after bait+punish is at least 150 cp worse than the engine's best alternative at the setup position.
    - Skip this test (`unittest.skipUnless`) when `build/sensei` is missing.
  - `test_live_trap`: after the setup moves of "Légal's mate", `live_trap` returns that trap; at the start position it returns `None`.
- [ ] **Step 2: Run the tests to verify they fail**
  - Run: the unittest command from Task 2.
  - Expected: `ModuleNotFoundError: coach.traps`.
- [ ] **Step 3: Write `traps.json` and `traps.py`.** Drop or fix any trap that fails `test_bait_really_loses`; don't loosen the threshold.
- [ ] **Step 4: Run the tests to verify they pass.**
- [ ] **Step 5: Commit** `feat(coach): verified opening trap list`

### Task 4: Move judge and messages

**Files:**
- Create: `coach/judge.py`
- Test: `tests/coach/test_judge.py`

**Interfaces:**
- Consumes: `MoveStat` (Task 2), `name_for` (Task 2).
- Produces:
  - `class Analyzer:`
    - `__init__(self, engine: chess.engine.SimpleEngine)`
    - `score(board) -> int`: centipawns from the side to move's point of view, at `Limit(depth=10, time=0.4)`. Mate is ±10000 minus the distance to mate.
    - `refutation(board) -> list[chess.Move]`: PV, at most 6 plies.
    - Before analysing, it always sets `UCI_LimitStrength` false.
  - `@dataclass Verdict: kind: Literal["book","offbook","mistake"]; loss: int; best: chess.Move | None; consequence: str | None`
  - `judge(before: chess.Board, move: chess.Move, masters: list[MoveStat] | None, human: list[MoveStat] | None, an: Analyzer) -> Verdict`. Rules, in order:
    - **book:** `move` is in `masters` with at least 10 games, or in `human` with at least 50 games and at least 5% of that position's total games.
    - **loss:** `an.score(before) - (-an.score(after))`.
    - **mistake:** `loss >= 150`. `consequence = describe(after, an.refutation(after), mover=before.turn)`.
    - **offbook:** anything else.
    - `best` is the top masters move when there is one, else the engine's best.
  - `describe(after: chess.Board, pv: list[chess.Move], mover: chess.Color) -> str`, using piece values P1 N3 B3 R5 Q9 along the PV:
    - mate in the PV → `f"it allows mate after {san0}"`
    - the mover's net loss is at least 3 → `f"it loses your {piece} to {san0}"`, where `piece` is the mover's most valuable piece captured in the PV
    - net loss of 1–2 → `f"it drops a pawn to {san0}"` (or `"two pawns"`)
    - otherwise → `f"it lets {Opponent} take over with {san0}"`
    - `san0` is the first PV move in SAN, with a move number (e.g. `"4...Qh4+"`). `Opponent` is "White" or "Black".
  - `fit(text: str, limit: int = 140) -> str`: if the text is too long, shorten opening names first to their `family()` before the colon, then hard-truncate with "…".
  - Message builders, returning text already passed through `fit()`:
    - `msg_mistake(v, move_san, book_san, book_name)` → `"{move_san} was a mistake: {consequence}. Book here is {book_san} ({book_name})."`
    - `msg_offbook(move_san, book_san, book_name, common_san)` → `"Book here is {book_san} ({book_name}); most played at your level: {common_san}. Your {move_san} is playable."`. Drop the "most played" clause when `common_san == book_san` or when there's no data.
    - `msg_trap_warning(t)` → `"Careful: {t.bait} here falls into {t.name}. {t.note}"`
    - `msg_trap_setter(t, victim)` → `"Trap set! If {victim} plays {t.bait}, {t.punish} wins: {t.name}."`
    - `msg_gambit(name, best_san)` → `"That's the {name}. The best answer is {best_san}."`
    - `msg_summary(name, left_at, book_san)` → either `"Opening over ({name}). You left book at move {left_at}; book was {book_san}."` or `"Opening over ({name}). You stayed in book the whole way. Nice!"`

- [ ] **Step 1: Write the failing tests** in `tests/coach/test_judge.py`. The engine tests skip when `build/sensei` is missing.
  - `test_book`: start position, `e2e4`, masters `[MoveStat("e2e4","e4",1000,…)]` → kind `book`.
  - `test_mistake_hangs_queen`:
    - Position: after `1.e4 e5 2.Qh5 Nc6 3.Bc4`.
    - Move: Black plays `g7g6`, which is fine (control case) → not `mistake`.
    - Move: instead Black plays `g8f6`, which allows `Qxf7#` → kind `mistake`, and consequence starts with `"it allows mate"`.
  - `test_mistake_loses_piece`:
    - Position: after `1.e4 e5 2.Nf3 Nc6 3.Bc4 Nd4` (Blackburne Shilling).
    - Move: White plays `f3e5`.
    - Expect: `mistake`, and consequence contains `"loses your"` or `"allows mate"`.
  - `test_offbook_ok`: start position, `g1h3`, with no masters or human data → kind `offbook` (Nh3 doesn't lose 150 cp).
  - `test_describe_pawn`: a hand-made PV where the mover loses exactly one pawn → `"it drops a pawn to …"`.
  - `test_messages_fit`: every `msg_*` builder, fed the longest name in `openings.tsv` and SAN `exd8=Q+`, returns ≤ 140 characters with no `"cp"` and no decimal eval.
- [ ] **Step 2: Run the tests to verify they fail.**
- [ ] **Step 3: Implement `judge.py`.**
- [ ] **Step 4: Run the tests to verify they pass.**
- [ ] **Step 5: Commit** `feat(coach): move judge and chat messages`

### Task 5: Coaching session

**Files:**
- Create: `coach/session.py`
- Test: `tests/coach/test_session.py`

**Interfaces:**
- Consumes: Explorer (Task 2), `find`/`family`/`name_for` (Task 2), `live_trap` (Task 3), `judge`/`Analyzer`/`msg_*` (Task 4).
- Produces:
  - `@dataclass Decision`:
    - `move: chess.Move | None`: `None` means let the engine search.
    - `full_strength: bool`
    - `messages: list[str]`
  - `class Session:`
    - `__init__(self, coach_color: chess.Color, explorer, analyzer, level: int = 1200)`
    - `command(self, text: str) -> str | None`: handles one chat line from the opponent and returns the reply, or `None` when it isn't a command.
    - `greeting(self) -> str`
    - `decide(self, board: chess.Board) -> Decision`: called on Sensei's turn.
    - `level: int | None` attribute: `None` means max.
    - `in_opening: bool` attribute.
- Copy:
  - **Greeting:** `"Hi! I'm Sensei, an opening coach. Set my strength with !level 400-2400 or max, practise one with !opening <name>. Now: 1200."` If that exceeds 140 characters, shorten the wording but keep both commands.
  - **`!level 800`** → `"Level set to 800."`. `!level max` → `"Level set to max."`.
  - **Bad level** → `"Use !level 400-2400 or !level max."` Numbers are clamped to 400–2400 and confirmed.
  - **`!opening italian`** → `"Practising the Italian Game. I'll play the other side."`
  - **Opening not found** → `"Couldn't find that opening. Try e.g. !opening sicilian."`
  - **Opening no longer reachable** → `"We've left the {family} lines, so I'm back to main lines."`, sent once.
  - **Off the chosen opening** → beginner moves that leave every remaining line of the chosen opening get `"{move_san} leaves the {family}; it continues {line_san}."` in place of the off-book message.
  - Commands are case-insensitive, and leading/trailing spaces are ignored.

**`decide` algorithm:**
- **Ply tracking:** keep `last_judged_ply`. Judge the beginner's last move only when `len(board.move_stack) > last_judged_ply`, then set it. This makes takebacks and restarts safe.
- **During the opening** (Global Constraints decide when it ends):
  1. If the last move was the beginner's, run `judge(before, move, masters(before), lichess(before, level or 2400), analyzer)`. Choose at most one message, by priority:
     1. mistake (sets `punish_next`)
     2. trap warning or trap setter, from `live_trap(board)`
     3. leaving the chosen opening
     4. off-book. Record `left_at` the first time.
     5. gambit: the name of the position after the beginner's move contains "Gambit" and the move is book.
  2. Sensei's move:
     - `punish_next` is set → `Decision(None, full_strength=True)`.
     - A chosen opening has a line consistent with the moves so far that continues → the most common next move among those lines.
     - Otherwise, the masters top move by `games` that is not a known trap `bait` for Sensei.
     - No masters data → `Decision(None, full_strength=True)`.
- **When the opening phase ends:** emit `msg_summary` once, then from then on return `Decision(None, full_strength = level is None)`.
- **Failures:** any exception from the explorer or analyzer inside `decide` is caught. Return `Decision(None, full_strength=in_opening, messages=[])` and log the error with `logging`.

- [ ] **Step 1: Write the failing tests** in `tests/coach/test_session.py`. Use a fake explorer from canned dicts keyed by epd, and a fake analyzer with canned `score`/`refutation`.
  - `test_command_parsing`:
    - `"!level 800"`, `"!LEVEL 800 "` → level 800.
    - `"!level 99999"` → 2400 and confirmed.
    - `"!level abc"` → the help reply.
    - `"!level max"` → `level is None`.
    - `"!opening xyzzy"` → not-found reply.
    - `"hello"` → `None`.
    - Every reply is ≤ 140 characters.
  - `test_plays_main_line`: masters says `e2e4` 1000 games and `d2d4` 900 at the start; Sensei is White → `Decision(move=e2e4)`.
  - `test_opening_choice_steers`: after `!opening italian` with Sensei as White, the moves are 1.e4 e5 2.Nf3 Nc6, and the fake masters top move is `d2d4` → Sensei plays `f1c4`.
  - `test_mistake_is_punished_and_explained`: the fake analyzer reports a 300 cp loss for the beginner's move → exactly one message starting with `"{san} was a mistake"`, and `full_strength=True`, `move=None`.
  - `test_one_message_per_move`: a position that is both off-book and a live trap → exactly one message, the trap one.
  - `test_explorer_failure_is_silent`: the explorer returns `None` everywhere and the analyzer raises → messages `[]`, `move=None`, and no exception.
  - `test_takeback_and_resume`:
    - Call `decide` at ply 6, then at ply 4 (takeback), then at ply 6 again → there is never a second message for the same ply, and nothing raises.
    - A fresh Session handed a 20-ply board → no greeting from `decide` and `in_opening` is False.
  - `test_opening_ends_by_move_15`: a fullmove number above 15 → the summary is emitted once, and later calls emit nothing about the opening.
  - `test_level_after_opening`: out of the opening with level 800 → `full_strength=False`; with level None → `True`.
- [ ] **Step 2: Run the tests to verify they fail.**
- [ ] **Step 3: Implement `session.py`.**
- [ ] **Step 4: Run the whole coach suite to verify it passes.**
- [ ] **Step 5: Commit** `feat(coach): per-game coaching session`

### Task 6: lichess-bot homemade engine and deploy wiring

**Files:**
- Create: `coach/lichess_engine.py`
- Create: `deploy/homemade.py`
- Modify: `deploy/lichess-bot.yml`
- Modify: `.github/workflows/ci.yml`
- Modify: `README.md`
- Test: `tests/coach/test_lichess_engine.py`

**Interfaces:**
- Consumes: `Session`, `Decision` (Task 5); `Explorer` (Task 2); `Analyzer` (Task 4); UCI options (Task 1).
- Produces:
  - `is_coaching_game(game) -> bool`: applies the Global Constraints rule to lichess-bot's `model.Game`, using `game.opponent.is_bot`, `game.mode`, `game.clock_initial`, `game.clock_increment`, `game.initial_fen` (must be empty or `"startpos"`) and `game.variant_name == "Standard"`.
  - `class SenseiCoach(MinimalEngine)`:
    - Uses lichess-bot's homemade engine contract: `__init__(commands, options, stderr, draw_or_resign, game, debug, **popen_args)` and `search(board, time_limit, ponder, draw_offered, root_moves) -> chess.engine.PlayResult`.
    - Starts `chess.engine.SimpleEngine.popen_uci(SENSEI_ENGINE)`.
      - `SENSEI_ENGINE` comes from env, defaulting to `<repo root>/build/sensei`, where the repo root is the parent of the `coach/` directory.
      - It is configured with `options` (Hash, Threads), passed through from `homemade_options` in the config.
    - Implements `quit()` to close the engine.
  - `class ChatClient`:
    - `__init__(game_id, token, session=None)`
    - `fetch() -> list[tuple[str, str]]`: (username, text) pairs, from `GET https://lichess.org/api/bot/game/{id}/chat`.
    - `post(text)`: `POST …/chat`, with `room=player`.
    - 2 s timeout. It never raises, and never logs the token.

**`search` behaviour:**
- **Non-coaching games**, in order:
  1. EGTB move when there are 7 pieces or fewer.
  2. Else a masters book move (highest score for the side to move among moves with ≥ 25 games), while out-of-book misses ≤ 10.
  3. Else `engine.play(board, time_limit, ponder=ponder)` at full strength.
  4. Book and EGTB lookups are skipped when less than 20 s remains on our clock, matching today's `min_time`.
- **Coaching games:**
  1. Fetch chat, and pass the opponent's lines not seen before to `session.command`, posting any replies.
  2. Post the greeting on the first call only when the board has fewer than 2 plies.
  3. On move 1 as White, keep polling chat for up to 15 s before deciding.
  4. Call `session.decide(board)` and post its messages.
  5. If `decision.move` is set, return it.
  6. Otherwise configure `UCI_LimitStrength`/`UCI_Elo` from `decision.full_strength` and `session.level`, then `engine.play(board, time_limit)`, with no ponder in coaching games.
- **Result:** return a `PlayResult` that carries the engine's `info` score, so lichess-bot's draw/resign logic keeps working.

**Config changes in `deploy/lichess-bot.yml`:**
- Set `engine.protocol: "homemade"` and `engine.name: "SenseiCoach"`.
- Rename `uci_options` to `homemade_options`, keeping Hash 256 and Threads 4.
- Remove the `online_moves` block, since book and EGTB move into the coach. Keep `ponder: true`.
- Keep `challenge` as-is.

**Other files:**
- `deploy/homemade.py`: puts `~/sensei-release` on `sys.path` (computed from the symlink target's real path, `Path(__file__).resolve().parents[1]`) and re-exports `SenseiCoach`.
- README "Running on Lichess": add `ln -sf ~/sensei-release/deploy/homemade.py ~/lichess-bot/homemade.py`, plus a short "Opening coach" section describing the commands and the conditions under which coaching is on.
- CI: add the step `python -m unittest discover -s tests/coach -t . -v` after the build, using the python that already has python-chess. Add `requests` to its pip install if it's missing.

- [ ] **Step 1: Write the failing tests** in `tests/coach/test_lichess_engine.py`. Add `~/lichess-bot` to `sys.path` inside the test, and skip if it's absent (CI).
  - `test_coaching_gate`: build `model.Game` objects from minimal `gameFull` dicts.
    - Coached: human, casual, 10+0 → True.
    - Not coached → False for each of:
      - a BOT opponent
      - rated
      - 5+3
      - `initialFen` set to a custom FEN
    - Coached: human, casual, 8+5 (estimated 8×60 + 40×5 = 680 s) → True.
  - `test_chat_client_never_raises`: a fake session that raises `ConnectionError` → `fetch() == []`, and `post` returns without raising.
- [ ] **Step 2: Run the tests to verify they fail.**
- [ ] **Step 3: Implement the files above** and update the config, README and CI.
- [ ] **Step 4: Run all suites**
  - Run: `cd build && ctest`
  - Run: `~/lichess-bot/venv/bin/python -m unittest discover -s tests/coach -t . -v`
  - Run: `~/lichess-bot/venv/bin/python tests/ponder_test.py`
  - Expected: all pass.
- [ ] **Step 5: Dry run of lichess-bot loading the class**, without connecting:
  - Run: `cd ~/lichess-bot && ln -sf ~/ChessEngine/deploy/homemade.py homemade.py && venv/bin/python -c "import homemade; print(homemade.SenseiCoach)"`
  - Expected: the class prints.
  - Afterwards, re-point the symlink at `~/sensei-release` (the deploy path), or note that deploy does it.
- [ ] **Step 6: Commit** `feat(coach): SenseiCoach lichess-bot engine, chat, deploy config`

### Task 7: Calibrate levels, open the PR, live check

**Files:**
- Modify (constants only): `src/search.cpp`
- Modify: `README.md` (results)

- [ ] **Step 1: Ladder match.** Run each adjacent pair with the existing match tool, 200 games at 10+0.1, e.g.:
  - Run: `tests/match.py build/sensei build/sensei --new-option UCI_LimitStrength=true --new-option UCI_Elo=800 --old-option UCI_LimitStrength=true --old-option UCI_Elo=400`
  - Pairs: 400/800, 800/1200, 1200/1600, 1600/2000, 2000/2400, and 2400 vs full strength.
  - Expected: each step scores between +100 and +350 Elo for the higher level.
- [ ] **Step 2: Adjust constants if needed.** If a step falls outside that range, adjust only the depth-cap slope and `T` constants from Task 1, re-run Task 1's tests, and repeat Step 1 for the affected pairs. Record the final ladder in the README results section, noting that absolute ratings are approximate.
- [ ] **Step 3: Commit** `tune(engine): calibrate UCI_Elo ladder`
- [ ] **Step 4: Push and open the PR.**
  - Run: `git push -u origin opening-coach`
  - Open with `gh pr create`. The body summarises the features, lists the test results and the ladder, and gives the deploy steps:
    - merge
    - `cd ~/sensei-release && git pull && cmake --build build -j`
    - `ln -sf ~/sensei-release/deploy/homemade.py ~/lichess-bot/homemade.py`
    - `launchctl kickstart -k gui/$(id -u)/com.aryanthakur.sensei-lichess-bot`
  - End the body with `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
- [ ] **Step 5: Live check after the user merges and deploys.** The user plays one casual 10+0 game as a human against BigElephant67, typing `!level 800` and `!opening italian` and making one deliberate blunder.
  - Expected: the greeting appears; both commands are confirmed; Sensei steers into the Italian; the blunder is explained in chat and punished on the board; an opening summary arrives by move 15.
  - Also confirm in `~/lichess-bot/bot.log` that a bot-vs-bot matchmaking game still plays normally.
