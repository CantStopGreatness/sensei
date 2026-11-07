import unittest
from pathlib import Path

import chess
import chess.engine

from coach.traps import TRAPS, live_trap

ENGINE = Path(__file__).resolve().parents[2] / "build" / "sensei"


def setup_board(trap):
    b = chess.Board()
    for san in trap.setup:
        b.push_san(san)
    return b


class TrapsTest(unittest.TestCase):
    def test_count(self):
        self.assertTrue(30 <= len(TRAPS) <= 50, len(TRAPS))
        self.assertEqual(len({t.name for t in TRAPS}), len(TRAPS))

    def test_legal(self):
        for t in TRAPS:
            with self.subTest(t.name):
                b = setup_board(t)
                b.push_san(t.bait)
                b.push_san(t.punish)

    def test_both_sides(self):
        victims = [setup_board(t).turn for t in TRAPS]
        self.assertGreaterEqual(victims.count(chess.WHITE), 5)
        self.assertGreaterEqual(victims.count(chess.BLACK), 5)

    def test_notes_short(self):
        for t in TRAPS:
            self.assertLessEqual(len(t.note), 60, t.name)

    @unittest.skipUnless(ENGINE.exists(), "needs build/sensei")
    def test_bait_really_loses(self):
        # Guards against confidently wrong advice: the bait must cost the victim 150+ cp.
        with chess.engine.SimpleEngine.popen_uci(str(ENGINE)) as engine:
            for t in TRAPS:
                with self.subTest(t.name):
                    b = setup_board(t)
                    victim = b.turn
                    best = engine.analyse(b, chess.engine.Limit(depth=12), game=object())["score"].pov(victim).score(mate_score=10000)
                    b.push_san(t.bait)
                    b.push_san(t.punish)
                    if b.is_checkmate():
                        after = -10000
                    else:
                        after = engine.analyse(b, chess.engine.Limit(depth=12), game=object())["score"].pov(victim).score(mate_score=10000)
                    self.assertGreaterEqual(best - after, 150, f"{t.name}: best {best}, after bait {after}")

    def test_live_trap(self):
        legal = next(t for t in TRAPS if t.name == "Légal's mate")
        self.assertIs(live_trap(setup_board(legal)), legal)
        self.assertIsNone(live_trap(chess.Board()))


if __name__ == "__main__":
    unittest.main()
