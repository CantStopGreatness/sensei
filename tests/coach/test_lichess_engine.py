import datetime
import sys
import unittest
from pathlib import Path

import requests

LICHESS_BOT = Path.home() / "lichess-bot"
if LICHESS_BOT.exists() and str(LICHESS_BOT) not in sys.path:
    sys.path.insert(0, str(LICHESS_BOT))


def game_full(white="BigElephant67", white_title="BOT", black="beginner", black_title=None, rated=False,
              initial=600_000, increment=0, variant="Standard", fen="startpos"):
    from lib import model
    info = {
        "id": "abcd1234", "speed": "rapid", "createdAt": 0, "rated": rated,
        "clock": {"initial": initial, "increment": increment},
        "perf": {"name": "Rapid"}, "variant": {"name": variant}, "initialFen": fen,
        "white": {"name": white, "title": white_title}, "black": {"name": black, "title": black_title},
        "state": {"moves": ""},
    }
    return model.Game(info, "BigElephant67", "https://lichess.org/", datetime.timedelta(seconds=20))


@unittest.skipUnless(LICHESS_BOT.exists(), "needs ~/lichess-bot")
class LichessEngineTest(unittest.TestCase):
    def test_coaching_gate(self):
        from coach.lichess_engine import is_coaching_game
        self.assertTrue(is_coaching_game(game_full()))
        self.assertFalse(is_coaching_game(game_full(black="otherbot", black_title="BOT")))
        self.assertFalse(is_coaching_game(game_full(rated=True)))
        self.assertFalse(is_coaching_game(game_full(initial=300_000, increment=3_000)))
        self.assertFalse(is_coaching_game(game_full(
            variant="From Position", fen="rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1")))
        self.assertTrue(is_coaching_game(game_full(initial=480_000, increment=5_000)))  # 8+5: 680 s

    def test_chat_client_never_raises(self):
        from coach.lichess_engine import ChatClient

        class Broken(requests.Session):
            def request(self, *args, **kwargs):
                raise requests.ConnectionError("down")

        chat = ChatClient("abcd1234", "token", Broken())
        self.assertEqual(chat.fetch(), [])
        chat.post("hello")  # must not raise


if __name__ == "__main__":
    unittest.main()
