import io
import unittest

import chess
import chess.pgn
import requests

from coach.review import StudyClient, review_pgn
from coach.session import Moment


def board_after(*sans):
    b = chess.Board()
    for s in sans:
        b.push_san(s)
    return b


def arrows(node):
    return [(a.tail, a.head, a.color) for a in node.arrows()]


class Reply:
    def __init__(self, status, data=None, text=""):
        self.status_code, self._data, self.text = status, data, text

    def json(self):
        return self._data


class FakeSession(requests.Session):
    """Answers requests from a {(method, path suffix): Reply} table and records each call."""

    def __init__(self, table):
        super().__init__()
        self.table, self.calls = table, []

    def request(self, method, url, **kwargs):
        self.calls.append((method, url, kwargs))
        for (m, suffix), reply in self.table.items():
            if m == method and url.endswith(suffix):
                if isinstance(reply, Exception):
                    raise reply
                return reply
        return Reply(404)


class ReviewPgnTest(unittest.TestCase):
    def test_comments_and_arrows_land_on_the_right_moves(self):
        board = board_after("e4", "e5", "Nf3", "Nc6", "Ba6", "bxa6")
        moments = [Moment(5, "3.Ba6 was a mistake.", [(chess.F1, chess.B5, "green"), (chess.F1, chess.A6, "red")])]
        game = chess.pgn.read_game(io.StringIO(review_pgn(board, moments, "Mouzen68", "BigElephant67")))
        self.assertEqual((game.headers["White"], game.headers["Black"]), ("Mouzen68", "BigElephant67"))
        nodes = list(game.mainline())
        self.assertEqual(len(nodes), 6)
        # Arrows on the position the beginner had to choose from; the words on their move.
        self.assertEqual(arrows(nodes[3]), [(chess.F1, chess.B5, "green"), (chess.F1, chess.A6, "red")])
        self.assertEqual(nodes[4].comment, "3.Ba6 was a mistake.")

    def test_moment_at_the_start_and_shared_nodes(self):
        board = board_after("e4", "e5")
        moments = [Moment(1, "Book here: e4.", [(chess.E2, chess.E4, "green")]), Moment(1, "Careful.", [])]
        game = chess.pgn.read_game(io.StringIO(review_pgn(board, moments, "a", "b")))
        self.assertEqual(arrows(game), [(chess.E2, chess.E4, "green")])
        self.assertEqual(game.next().comment, "Book here: e4. Careful.")


class StudyClientTest(unittest.TestCase):
    def test_publish_creates_imports_and_drops_the_empty_chapter(self):
        export = ('[Event "x: Chapter 1"]\n[ChapterURL "https://lichess.org/study/abc/empty1"]\n\n *\n\n\n'
                  '[Event "x: review"]\n[ChapterURL "https://lichess.org/study/abc/real2"]\n\n1. e4 *\n')
        fake = FakeSession({("POST", "/api/study"): Reply(200, {"id": "abc"}),
                            ("POST", "/api/study/abc/import-pgn"): Reply(200, {"chapters": [{"id": "real2"}]}),
                            ("GET", "/api/study/abc.pgn"): Reply(200, text=export),
                            ("DELETE", "/api/study/abc/empty1"): Reply(204)})
        url = StudyClient("tok", fake).publish("Sensei review", "1. e4 *", chess.WHITE)
        self.assertEqual(url, "https://lichess.org/study/abc/real2")
        self.assertEqual([(m, u.rsplit("/", 1)[-1]) for m, u, _ in fake.calls],
                         [("POST", "study"), ("POST", "import-pgn"), ("GET", "abc.pgn"), ("DELETE", "empty1")])
        create = fake.calls[0][2]["data"]
        self.assertEqual(create["visibility"], "unlisted")
        self.assertEqual(fake.calls[1][2]["data"]["orientation"], "white")
        self.assertEqual(fake.calls[0][2]["headers"], {"Authorization": "Bearer tok"})

    def test_publish_without_permission_or_network_is_silent(self):
        fake = FakeSession({("POST", "/api/study"): Reply(403, {"error": "Missing scope"})})
        self.assertIsNone(StudyClient("tok", fake).publish("n", "1. e4 *", chess.BLACK))
        self.assertEqual(len(fake.calls), 1)
        down = FakeSession({("POST", "/api/study"): requests.ConnectionError("down")})
        self.assertIsNone(StudyClient("tok", down).publish("n", "1. e4 *", chess.BLACK))

    def test_failed_cleanup_still_returns_the_study(self):
        fake = FakeSession({("POST", "/api/study"): Reply(200, {"id": "abc"}),
                            ("POST", "/api/study/abc/import-pgn"): Reply(200, {"chapters": [{"id": "real2"}]}),
                            ("GET", "/api/study/abc.pgn"): requests.Timeout()})
        self.assertEqual(StudyClient("tok", fake).publish("n", "1. e4 *", chess.WHITE),
                         "https://lichess.org/study/abc/real2")


if __name__ == "__main__":
    unittest.main()
