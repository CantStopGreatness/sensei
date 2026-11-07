"""Judge a beginner's move against the books and the engine, and word the chat messages."""
from dataclasses import dataclass
from typing import Literal

import chess
import chess.engine

from coach.explorer import MoveStat

MISTAKE_CP = 150
MATE = 10000
VALUE = {chess.PAWN: 1, chess.KNIGHT: 3, chess.BISHOP: 3, chess.ROOK: 5, chess.QUEEN: 9}
COLOR = {chess.WHITE: "White", chess.BLACK: "Black"}


class Analyzer:
    """Full-strength Sensei analysis, whatever level the game is set to."""

    def __init__(self, engine: chess.engine.SimpleEngine):
        self.engine = engine
        self.limit = chess.engine.Limit(depth=10, time=0.4)

    def _analyse(self, board: chess.Board) -> dict:
        self.engine.configure({"UCI_LimitStrength": False})
        return self.engine.analyse(board, self.limit)

    def score(self, board: chess.Board) -> int:
        """Centipawns for the side to move; mate is +-(10000 - distance)."""
        if board.is_checkmate():
            return -MATE
        if board.is_game_over():
            return 0
        return self._analyse(board)["score"].relative.score(mate_score=MATE)

    def refutation(self, board: chess.Board) -> list[chess.Move]:
        if board.is_game_over():
            return []
        return self._analyse(board).get("pv", [])[:6]


@dataclass
class Verdict:
    kind: Literal["book", "offbook", "mistake"]
    loss: int
    best: chess.Move | None
    consequence: str | None
    from_book: bool = False  # best came from the masters book, not the engine


def _is_book(move: chess.Move, masters: list[MoveStat] | None, human: list[MoveStat] | None) -> bool:
    uci = move.uci()
    if masters and any(s.uci == uci and s.games >= 10 for s in masters):
        return True
    if human:
        total = sum(s.games for s in human)
        return any(s.uci == uci and s.games >= 50 and s.games >= 0.05 * total for s in human)
    return False


def judge(before: chess.Board, move: chess.Move, masters: list[MoveStat] | None,
          human: list[MoveStat] | None, an: Analyzer) -> Verdict:
    best = chess.Move.from_uci(masters[0].uci) if masters else None
    from_book = best is not None
    if _is_book(move, masters, human):
        return Verdict("book", 0, best, None, from_book)
    after = before.copy()
    after.push(move)
    after_score = an.score(after)  # for the opponent
    loss = an.score(before) + after_score
    if best is None:
        best = next(iter(an.refutation(before)), None)
    if loss >= MISTAKE_CP:
        pv = an.refutation(after)
        return Verdict("mistake", loss, best, describe(after, pv, before.turn, mate=after_score >= MATE - 100), from_book)
    return Verdict("offbook", loss, best, None, from_book)


def numbered_san(board: chess.Board, move: chess.Move) -> str:
    dots = "." if board.turn == chess.WHITE else "..."
    return f"{board.fullmove_number}{dots}{board.san(move)}"


def describe(after: chess.Board, pv: list[chess.Move], mover: chess.Color, mate: bool = False) -> str:
    """Plain words for what the refutation line does to the mover."""
    if not pv:
        return f"it lets {COLOR[not mover]} take over"
    san0 = numbered_san(after, pv[0])
    b = after.copy()
    net, biggest = 0, None
    for m in pv:
        if not b.is_legal(m):
            break
        captured = b.piece_at(m.to_square) or (b.is_en_passant(m) and chess.Piece(chess.PAWN, not b.turn))
        if captured and captured.piece_type in VALUE:
            v = VALUE[captured.piece_type]
            if captured.color == mover:
                net += v
                if biggest is None or v > VALUE[biggest]:
                    biggest = captured.piece_type
            else:
                net -= v
        b.push(m)
        if b.is_checkmate():
            mate = True
    if mate:
        return f"it allows mate after {san0}"
    if net >= 3 and biggest:
        return f"it loses your {chess.piece_name(biggest)} to {san0}"
    if net >= 1:
        return f"it drops {'a pawn' if net == 1 else 'two pawns'} to {san0}"
    return f"it lets {COLOR[not mover]} take over with {san0}"


def fit(text: str, limit: int = 140, names: tuple[str, ...] = ()) -> str:
    """At most `limit` characters: shorten opening names to their family first, then cut."""
    for name in names:
        if len(text) <= limit:
            break
        if name and ":" in name:
            text = text.replace(name, name.split(":")[0])
    return text if len(text) <= limit else text[:limit - 1].rstrip() + "…"


def msg_mistake(v: Verdict, move_san: str, book_san: str | None, book_name: str | None) -> str:
    text = f"{move_san} was a mistake: {v.consequence}."
    if book_san and v.from_book:
        text += f" Book here is {book_san}" + (f" ({book_name})." if book_name else ".")
    elif book_san:
        text += f" Better was {book_san}."  # the engine's move: never call it book
    return fit(text, names=(book_name or "",))


def msg_offbook(move_san: str, book_san: str, book_name: str | None, common_san: str | None) -> str:
    text = f"Book here is {book_san}" + (f" ({book_name})" if book_name else "")
    if common_san and common_san != book_san:
        text += f"; most played at your level: {common_san}"
    return fit(text + f". Your {move_san} is playable.", names=(book_name or "",))


def msg_trap_warning(t) -> str:
    return fit(f"Careful: {t.bait} here falls into {t.name}. {t.note}", names=(t.name,))


def msg_trap_setter(t, victim: str) -> str:
    return fit(f"Trap set! If {victim} plays {t.bait}, {t.punish} wins: {t.name}.", names=(t.name,))


def msg_gambit(name: str, best_san: str) -> str:
    return fit(f"That's the {name}. A strong reply is {best_san}.", names=(name,))


def msg_summary(name: str | None, left_at: int | None, book_san: str | None) -> str:
    head = f"Opening over ({name})." if name else "Opening over."
    if left_at:
        text = f"{head} You left book at move {left_at}" + (f"; book was {book_san}." if book_san else ".")
    else:
        text = f"{head} You stayed in book the whole way. Nice!"
    return fit(text, names=(name or "",))
