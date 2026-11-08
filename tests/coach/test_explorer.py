import unittest

import chess
import requests

from coach.explorer import Explorer, MoveStat


class FakeResponse:
    def __init__(self, status, data):
        self.status_code, self._data = status, data

    def json(self):
        return self._data


class FakeSession(requests.Session):
    """Answers every get with the next canned item; an exception instance is raised."""

    def __init__(self, *answers):
        super().__init__()
        self.answers, self.calls = list(answers), 0

    def get(self, url, **kwargs):
        self.calls += 1
        a = self.answers.pop(0) if len(self.answers) > 1 else self.answers[0]
        if isinstance(a, Exception):
            raise a
        return a


def move(uci, san, games):
    return {"uci": uci, "san": san, "white": games // 2, "draws": games // 4, "black": games - games // 2 - games // 4}


class ExplorerTest(unittest.TestCase):
    def test_parses_and_sorts(self):
        fake = FakeSession(FakeResponse(200, {"moves": [move("e2e4", "e4", 100), move("d2d4", "d4", 300)]}))
        stats = Explorer(fake).masters(chess.Board())
        self.assertEqual([s.uci for s in stats], ["d2d4", "e2e4"])
        self.assertEqual([s.san for s in stats], ["d4", "e4"])
        self.assertEqual(stats[0].games, 300)
        self.assertIsInstance(stats[0], MoveStat)

    def test_error_returns_none(self):
        fake = FakeSession(requests.Timeout(), FakeResponse(429, {}))
        e = Explorer(fake)
        self.assertIsNone(e.masters(chess.Board()))
        self.assertIsNone(e.lichess(chess.Board(), 1200))

    def test_cache(self):
        fake = FakeSession(FakeResponse(200, {"moves": [move("e2e4", "e4", 10)]}))
        e = Explorer(fake)
        e.masters(chess.Board())
        e.masters(chess.Board())
        self.assertEqual(fake.calls, 1)

    def test_tablebase_best(self):
        fake = FakeSession(FakeResponse(200, {"moves": [{"uci": "a7a8q"}, {"uci": "a7a8r"}]}))
        board = chess.Board("8/P7/8/8/8/8/k7/4K3 w - - 0 1")
        self.assertEqual(Explorer(fake).tablebase_best(board), chess.Move.from_uci("a7a8q"))

    def test_castling_is_normalised(self):
        board = chess.Board("r1bqkb1r/pppp1ppp/2n2n2/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4")
        fake = FakeSession(FakeResponse(200, {"moves": [move("e1h1", "O-O", 100)]}))
        self.assertEqual(Explorer(fake).masters(board)[0].uci, "e1g1")

    def test_circuit_breaker(self):
        # After a failure the explorer stays quiet for a minute instead of blocking every lookup.
        fake = FakeSession(requests.Timeout())
        e = Explorer(fake)
        self.assertIsNone(e.masters(chess.Board()))
        b = chess.Board()
        b.push_san("e4")
        self.assertIsNone(e.lichess(b, 1200))
        self.assertIsNone(e.masters(b))
        self.assertEqual(fake.calls, 1)


if __name__ == "__main__":
    unittest.main()
