# Sensei opening coach — design

## Intent

Turn the Lichess bot **BigElephant67** into a tool that teaches beginners openings. The point is
not engine strength in the middlegame or endgame. During the opening, Sensei cross-references
opening databases with its own engine analysis and explains in the game chat what the beginner
should know. After the opening it plays the rest of the game at a strength the beginner chose.

Success: a beginner who plays a few games comes away knowing the main lines of the opening they
played, the common replies they will meet, the traps in it, and why their mistakes were mistakes,
because Sensei both told them in chat and showed them on the board.

## Who it's for

Beginners on Lichess, roughly 400–1500, who challenge BigElephant67 to a game.

## Which games get coaching

Coaching mode is on only when **all** of these hold (decided by the user):
- The opponent is a human, not a BOT account.
- The game is casual. Rated games against humans play at full strength with no coaching and no
  `!level`, so nobody can farm rating.
- The time control is rapid or slower (10+0 and up). Faster games play normally.

Every other game (bots, matchmaking, rated, blitz/bullet) plays exactly as the bot does today, at
full strength with the current opening book. Matchmaking against bots stays on.

At the start of a coaching game, Sensei posts a short greeting listing `!level` and `!opening`,
and waits about 15 seconds for commands before its first move when it is White.

## Behaviour during the opening

The opening phase lasts while the position is still in the opening databases (master games or
Lichess games), capped at move 15 *(my call)*.

Sensei comments in chat on the beginner's moves and the position:

1. **Common human moves.** The moves most often played in this position by real people, from the
   Lichess games database. Filter it to players near the chosen level *(my call)*, so it shows
   what the beginner will actually face.
2. **Main line.** What strong players play, from the masters database.
3. **Traps.** Well-known traps that are live in the position, for either side. Example: "careful,
   Nxe5 walks into Légal's mate."
4. **Best move / refutation.** What the engine and theory say is strongest. For gambits this
   includes the known refutation.
5. **Leaving book.** Whenever the beginner plays a move that is not in the book, Sensei names the
   main line. Example: "Book here is 3.Bb5, the Ruy Lopez; your 3.Bd3 is playable but blocks your
   d-pawn."
6. **Mistakes.** When the move is not just off-book but actually bad, Sensei also explains what
   goes wrong. Example: "Bxf7+ was a mistake: it gives away your bishop for a pawn. The usual move
   here is Nf3, which develops and attacks e5." A mistake is detected by Sensei's own search as an
   eval drop past a threshold *(my call; the builder picks the threshold)*. The explanation is
   built from the refutation line, meaning what it wins or threatens.
7. **Punish mistakes.** After a mistake, Sensei plays the refutation, not a gentle move, so the
   beginner feels the consequence. This holds even when the chosen level is low.

**Chat volume.** Lichess caps chat messages at 140 characters and mutes bots that post too often.
So Sensei speaks only when something matters: the beginner leaves book, makes a mistake, or a trap
appears. It does not narrate every move. Common moves, main line and best move get folded into
those messages, not posted on their own every move *(my call)*.

### Sensei's own opening moves

- **Default:** Sensei plays common main lines (popular master moves), so the beginner meets real
  theory.
- **`!opening <name>`**, for example `!opening italian`: this is the opening the **beginner** wants
  to practise. Sensei plays the other side, choosing replies that keep the game in that opening's
  lines (the common ones beginners will meet). It also flags when the beginner strays from the
  opening they chose, not just from the overall main line. If the game can no longer reach that
  opening, Sensei says so and returns to main lines. It is meant to be typed during the start-of-game
  pause, but it is accepted any time while the opening phase lasts.

### End of the opening

When the opening phase ends, Sensei posts a one-line summary, such as "you left book at move 6;
the main line was …" *(my call)*.

## Behaviour after the opening

Sensei plays the rest of the game, through the endgame, at the level the player chose.

- **`!level <rating>`**, for example `!level 800`, from 400 up to Sensei's full strength. Sensei
  confirms the setting in chat.
- **Default level when not set: 1200** *(my call)*.
- `!level` (and `!opening`) apply to the current game only and reset each game *(my call)*.
- Strength is limited in the engine through UCI strength limiting (`UCI_LimitStrength` /
  `UCI_Elo`). These options don't exist yet: the C++ engine needs them added. Ratings are approximate, and calibrating them at the low end is rough. Mapping a
  rating to engine behaviour (depth or node limits, deliberate inaccuracies, and so on) is left to
  the builder.

## Constraints

- It runs inside the existing Lichess deployment: lichess-bot (Python, upstream clone in
  `~/lichess-bot`, currently unmodified), Sensei UCI engine (C++17, `~/ChessEngine`), deployed
  from the release clone `~/sensei-release` under the launchd agent.
- The coaching code lives in the sensei repo, in Python *(my call)*. Python fits because chat and
  opening database lookups happen in lichess-bot, which is Python. Hook into lichess-bot through its
  extension points where possible (`homemade.py` engine class, `extra_game_handlers.py`). Chat has
  no extension point, so it will need posting through the bot chat API or a thin wrapper. Keep
  upstream patches minimal so lichess-bot can still be updated. How exactly is left to the builder.
- Data sources: the Lichess opening explorer API (`lichess` database with rating filters, and
  `masters`). Respect its rate limits and fall back gracefully when it's unavailable, staying
  silent instead of saying something wrong.
- Traps: neither the explorer nor the engine knows traps. Build a hand-curated list of about 30–50
  well-known opening traps, each matched by position or move sequence, with a short explanation
  that fits in 140 characters.
- Never commit or print the Lichess token.
- Commits are authored as Aryan Thakur <aryan.thakur1508@gmail.com>.

## Non-goals

- Making Sensei stronger in the middlegame or endgame.
- Teaching outside the opening, such as middlegame plans or endgame technique.
- A website, app, or UI outside Lichess chat.

## Anti-goals (failure even if it technically works)

- Spamming chat, or getting the bot muted or rate-limited by Lichess.
- Confidently wrong advice: calling a sound move a mistake, naming the wrong main line, or
  announcing a trap that isn't there. When unsure, say nothing.
- Advice in engine jargon beginners can't follow, such as "-1.3, Nf6 is best". Explanations must
  be plain language.
- Breaking the existing bot: crashes, hung games, or losing on time because of slow explorer
  lookups.

## Decided (by the user)

- Platform: Lichess, explanations in the game chat.
- Comment both when the beginner leaves book (name the main line) and when they make a mistake
  (explain it).
- Punish mistakes on the board.
- During the opening, show common human moves, traps, and best moves / gambit refutations.
- After the opening, play at the player-chosen level, 400 to max, through the endgame.
- Choose the level with a chat command, `!level <rating>`.
- Main lines by default; `!opening <name>` to choose an opening.
- Coaching only for human opponents in casual games at rapid or slower; everything else plays as today.
- `!opening` names the beginner's opening to practise; Sensei plays the other side and waits at the
  start for commands.

## Left to the builder

- Integration mechanics with lichess-bot.
- The mistake threshold and how explanations are generated from the refutation line.
- Which traps to include and how they're matched.
- Opening-name lookup for `!opening` (for example, ECO names from the Lichess chess-openings data).
- Strength-limiting implementation and calibration.
- Message wording, within the 140-character limit and plain-language rule.
