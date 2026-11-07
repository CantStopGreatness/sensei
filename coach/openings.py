"""Opening names from lichess-org/chess-openings (data/openings.tsv)."""
from dataclasses import dataclass
from pathlib import Path

import chess
import chess.pgn


@dataclass
class Opening:
    eco: str
    name: str
    moves: list[chess.Move]


def _load() -> tuple[list[Opening], dict[str, str]]:
    openings, by_epd = [], {}
    lines = (Path(__file__).parent / "data" / "openings.tsv").read_text().splitlines()[1:]
    for line in lines:
        eco, name, pgn = line.split("\t")
        board, moves = chess.Board(), []
        for token in pgn.split():
            if token[0].isdigit():
                continue  # move number like "1." or "12..."
            moves.append(board.push_san(token))
        openings.append(Opening(eco, name, moves))
        # The deepest line ending in a position names it.
        epd = board.epd()
        if epd not in by_epd or len(moves) >= by_epd[epd][0]:
            by_epd[epd] = (len(moves), name)
    return openings, {epd: name for epd, (_, name) in by_epd.items()}


OPENINGS, _BY_EPD = _load()


def name_for(board: chess.Board) -> str | None:
    return _BY_EPD.get(board.epd())


def find(query: str) -> list[Opening]:
    words = query.lower().split()
    if not words:
        return []
    hits = [o for o in OPENINGS if all(w in o.name.lower() for w in words)]
    return sorted(hits, key=lambda o: (len(o.name), len(o.moves)))


def family(op: Opening) -> str:
    return op.name.split(":")[0]
