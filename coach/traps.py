"""Hand-picked opening traps (data/traps.json), each checked by tests/coach/test_traps.py."""
import json
from dataclasses import dataclass
from pathlib import Path

import chess


@dataclass
class Trap:
    name: str
    setup: list[str]  # SAN from the start to the position where the victim is to move
    bait: str         # the victim's tempting losing move
    punish: str       # the refuting reply
    note: str         # why it loses, at most 60 characters


def _load() -> tuple[list[Trap], dict[str, Trap]]:
    traps = [Trap(**t) for t in json.loads((Path(__file__).parent / "data" / "traps.json").read_text())]
    by_epd = {}
    for t in traps:
        b = chess.Board()
        for san in t.setup:
            b.push_san(san)
        by_epd[b.epd()] = t
    return traps, by_epd


TRAPS, _BY_EPD = _load()


def live_trap(board: chess.Board) -> Trap | None:
    """The trap whose victim is to move in this position, if any."""
    return _BY_EPD.get(board.epd())
