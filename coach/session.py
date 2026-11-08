"""One coaching game: chat commands, judging the beginner's moves, and choosing Sensei's moves."""
import logging
from collections import Counter
from dataclasses import dataclass, field

import chess

from coach.judge import (COLOR, fit, judge, msg_gambit, msg_mistake, msg_offbook, msg_summary,
                         msg_trap_setter, msg_trap_warning, numbered_san)
from coach.openings import Opening, family, find, name_for
from coach.traps import live_trap

log = logging.getLogger(__name__)

MAX_OPENING_MOVE = 15
MIN_MASTERS, MIN_HUMAN = 5, 50
GAMBIT_MIN_GAMES = 25


@dataclass
class Decision:
    move: chess.Move | None  # None: let the engine search
    full_strength: bool
    messages: list[str] = field(default_factory=list)


class Session:
    def __init__(self, coach_color: chess.Color, explorer, analyzer, level: int | None = 1200):
        self.color = coach_color
        self.explorer, self.analyzer = explorer, analyzer
        self.level = level  # None = max
        self.in_opening = True
        self.lines: list[Opening] = []  # the chosen opening's lines, if any
        self.opening_name: str | None = None
        self.notified_unreachable = False
        self.last_judged_ply = 0
        self.first_call = True
        self.punish_next = False
        self.left_at: int | None = None
        self.left_book_san: str | None = None
        self.gambit_said = False

    # ---- chat ---------------------------------------------------------------------------------

    def greeting(self) -> str:
        now = "max" if self.level is None else self.level
        return fit("Hi! I'm Sensei, an opening coach. Set my strength with !level 400-2400 or max, "
                   f"practise one with !opening <name>. Now: {now}.")

    def command(self, text: str) -> str | None:
        words = text.strip().split()
        if not words:
            return None
        cmd, arg = words[0].lower(), " ".join(words[1:]).lower()
        if cmd == "!level":
            if arg == "max":
                self.level = None
                return "Level set to max."
            if not arg.isdigit():
                return "Use !level 400-2400 or !level max."
            self.level = max(400, min(2400, int(arg)))
            return f"Level set to {self.level}."
        if cmd == "!opening":
            found = find(arg)
            if not found:
                return "Couldn't find that opening. Try e.g. !opening sicilian."
            self.lines, self.opening_name, self.notified_unreachable = found, found[0].name, False
            return fit(f"Practising the {self.opening_name}. I'll play the other side.")
        return None

    # ---- moves --------------------------------------------------------------------------------

    def decide(self, board: chess.Board) -> Decision:
        try:
            return self._decide(board)
        except Exception:
            log.exception("coach failed; playing an engine move")
            return Decision(None, self.in_opening, [])

    def _decide(self, board: chess.Board) -> Decision:
        ply = len(board.move_stack)
        if self.first_call and ply > 2:
            self.in_opening = False  # resumed mid-game (restart): don't comment on moves we didn't see
        self.first_call = False
        if not self.in_opening:
            return Decision(None, self.level is None, [])

        messages = []
        beginner_moved = ply > 0 and board.turn == self.color
        if beginner_moved and ply > self.last_judged_ply:
            self.last_judged_ply = ply
            msg = self._comment_on_beginner(board)
            if msg:
                messages.append(msg)

        if self._opening_over(board):
            self.in_opening = False
            name = name_for(board) or self._last_name(board)
            messages.append(msg_summary(name, self.left_at, self.left_book_san))
            return Decision(None, self.level is None, messages)

        if self.punish_next:
            self.punish_next = False
            return Decision(None, True, messages)

        move = self._opening_move(board, messages)
        if move and not messages:
            after = board.copy()
            after.push(move)
            trap = live_trap(after)
            if trap and after.turn != self.color:
                messages.append(msg_trap_warning(trap))
        return Decision(move, True, messages)

    def _comment_on_beginner(self, board: chess.Board) -> str | None:
        before = board.copy()
        move = before.pop()
        masters = self.explorer.masters(before)
        human = self.explorer.lichess(before, self.level or 2400)
        v = judge(before, move, masters, human, self.analyzer)
        move_san = numbered_san(before, move)
        book_san = book_name = None
        if v.best and before.is_legal(v.best):
            book_san = numbered_san(before, v.best)
            after_book = before.copy()
            after_book.push(v.best)
            book_name = name_for(after_book)

        if v.kind == "mistake":
            self.punish_next = True
            self._note_left_book(before, book_san if v.from_book else None)
            return msg_mistake(v, move_san, book_san, book_name)

        trap = live_trap(board)
        if trap and board.turn == self.color:
            return msg_trap_setter(trap, COLOR[self.color])

        stray = self._stray(before, move)
        if stray:
            return stray

        if v.kind == "offbook" and masters:
            self._note_left_book(before, book_san)
            common = human[0] if human else None
            common_san = numbered_san(before, chess.Move.from_uci(common.uci)) if common else None
            return msg_offbook(move_san, book_san, book_name, common_san)

        # Once a game, when the beginner's own move offers a gambit: name the strong reply.
        name, before_name = name_for(board), name_for(before) or ""
        if (v.kind == "book" and not self.gambit_said and name and "Gambit" in name
                and "Declined" not in name and "Gambit" not in before_name):
            answer = self._gambit_answer(board)
            if answer:
                self.gambit_said = True
                return msg_gambit(name, numbered_san(board, answer))
        return None

    def _note_left_book(self, before: chess.Board, book_san: str | None) -> None:
        if self.left_at is None:
            self.left_at, self.left_book_san = before.fullmove_number, book_san

    def _continuing(self, board: chess.Board) -> list[Opening]:
        """Chosen-opening lines that match the game so far and have a next move."""
        played = board.move_stack
        return [o for o in self.lines if len(o.moves) > len(played) and o.moves[:len(played)] == played]

    def _stray(self, before: chess.Board, move: chess.Move) -> str | None:
        lines = self._continuing(before)
        if not lines or any(o.moves[len(before.move_stack)] == move for o in lines):
            return None
        expected = Counter(o.moves[len(before.move_stack)] for o in lines).most_common(1)[0][0]
        fam = family(lines[0])
        self.lines, self.notified_unreachable = [], True
        return fit(f"{numbered_san(before, move)} leaves the {fam}; it continues {numbered_san(before, expected)}.")

    def _opening_over(self, board: chess.Board) -> bool:
        if board.fullmove_number > MAX_OPENING_MOVE:
            return True
        masters = self.explorer.masters(board)
        human = self.explorer.lichess(board, self.level or 2400)
        if masters is None or human is None:
            return False  # unknown: keep going until move 15
        return sum(s.games for s in masters) < MIN_MASTERS and sum(s.games for s in human) < MIN_HUMAN

    def _opening_move(self, board: chess.Board, messages: list[str]) -> chess.Move | None:
        if self.lines:
            lines = self._continuing(board)
            if lines:
                return Counter(o.moves[len(board.move_stack)] for o in lines).most_common(1)[0][0]
            if not self.notified_unreachable and not any(o.moves == board.move_stack for o in self.lines):
                self.notified_unreachable = True
                fam = family(self.lines[0])
                self.lines = []
                if not messages:
                    messages.append(fit(f"We've left the {fam} lines, so I'm back to main lines."))
        trap = live_trap(board)
        bait = trap.bait if trap and board.turn == self.color else None
        name = name_for(board)
        if name and "Gambit" in name:
            answer = self._gambit_answer(board, avoid=bait)
            if answer:
                return answer
        for s in self.explorer.masters(board) or []:
            m = chess.Move.from_uci(s.uci)
            if board.is_legal(m) and board.san(m) != bait:
                return m
        return None

    def _gambit_answer(self, board: chess.Board, avoid: str | None = None) -> chess.Move | None:
        """The best-scoring masters reply for the side to move (never the `avoid` SAN)."""
        best, best_score = None, -1.0
        for s in self.explorer.masters(board) or []:
            if s.games < GAMBIT_MIN_GAMES:
                continue
            wins = s.white if board.turn == chess.WHITE else s.black
            score = (wins + s.draws / 2) / s.games
            m = chess.Move.from_uci(s.uci)
            if score > best_score and board.is_legal(m) and board.san(m) != avoid:
                best, best_score = m, score
        return best

    def _last_name(self, board: chess.Board) -> str | None:
        b = board.copy()
        while b.move_stack:
            b.pop()
            if name := name_for(b):
                return name
        return None
