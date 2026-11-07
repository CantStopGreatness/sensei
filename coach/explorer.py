"""Lichess opening explorer and tablebase lookups. Never raises: any failure gives None."""
import logging
import os
import time
from dataclasses import dataclass

import chess
import requests

log = logging.getLogger(__name__)

EXPLORER = "https://explorer.lichess.ovh"
TABLEBASE = "https://tablebase.lichess.ovh/standard"
RATING_BUCKETS = [0, 1000, 1200, 1400, 1600, 1800, 2000, 2200, 2500]
BACKOFF = 60  # seconds without lookups after any failure, so a dead explorer never stalls moves


@dataclass
class MoveStat:
    uci: str
    san: str
    games: int
    white: int
    draws: int
    black: int


def rating_buckets(rating: int) -> str:
    """The two explorer rating buckets nearest `rating`, e.g. 1200 -> "1000,1200"."""
    nearest = sorted(RATING_BUCKETS, key=lambda b: abs(b - rating))[:2]
    return ",".join(str(b) for b in sorted(nearest))


class Explorer:
    def __init__(self, session: requests.Session | None = None, timeout: float = 2.0):
        self.session = session or requests.Session()
        self.timeout = timeout
        self.cache: dict[tuple, object] = {}
        self.down_until = 0.0
        token = os.environ.get("LICHESS_BOT_TOKEN")
        self.headers = {"Authorization": f"Bearer {token}"} if token else {}

    def _get(self, url: str, params: dict) -> dict | None:
        if time.monotonic() < self.down_until:
            return None
        try:
            r = self.session.get(url, params=params, headers=self.headers, timeout=self.timeout)
            if r.status_code == 200:
                return r.json()
            log.info("explorer %s -> HTTP %s", url, r.status_code)
        except Exception as e:  # network errors, timeouts, bad JSON: stay silent
            log.info("explorer %s failed: %s", url, type(e).__name__)
        self.down_until = time.monotonic() + BACKOFF
        return None

    def _moves(self, board: chess.Board, key: tuple, url: str, params: dict) -> list[MoveStat] | None:
        if key not in self.cache:
            data = self._get(url, params)
            stats = None
            if data is not None:
                # parse_uci turns the explorer's king-takes-rook castling (e1h1) into e1g1.
                stats = sorted((MoveStat(board.parse_uci(m["uci"]).uci(), m["san"], m["white"] + m["draws"] + m["black"],
                                         m["white"], m["draws"], m["black"]) for m in data.get("moves", [])),
                               key=lambda s: -s.games)
            if stats is None:
                return None  # don't cache failures: the next call may succeed
            self.cache[key] = stats
        return self.cache[key]

    def masters(self, board: chess.Board) -> list[MoveStat] | None:
        fen = board.fen()
        return self._moves(board, ("masters", board.epd()), f"{EXPLORER}/masters", {"fen": fen, "moves": 12})

    def lichess(self, board: chess.Board, rating: int) -> list[MoveStat] | None:
        buckets = rating_buckets(rating)
        return self._moves(board, ("lichess", board.epd(), buckets), f"{EXPLORER}/lichess",
                           {"variant": "standard", "speeds": "rapid,classical", "ratings": buckets,
                            "fen": board.fen(), "moves": 12})

    def tablebase_best(self, board: chess.Board) -> chess.Move | None:
        key = ("tablebase", board.epd())
        if key not in self.cache:
            data = self._get(TABLEBASE, {"fen": board.fen()})
            if not data or not data.get("moves"):
                return None
            self.cache[key] = chess.Move.from_uci(data["moves"][0]["uci"])
        return self.cache[key]
