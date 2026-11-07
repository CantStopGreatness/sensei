import re
import unittest
from pathlib import Path

import chess
import chess.engine

from coach.explorer import MoveStat
from coach.judge import (Analyzer, Verdict, describe, judge, msg_gambit, msg_mistake, msg_offbook,
                         msg_summary, msg_trap_setter, msg_trap_warning)
from coach.openings import OPENINGS
from coach.traps import Trap

ENGINE = Path(__file__).resolve().parents[2] / "build" / "sensei"


def board_after(*sans):
    b = chess.Board()
    for s in sans:
        b.push_san(s)
    return b


def stat(uci, san, games):
    return MoveStat(uci, san, games, games // 2, games // 4, games - games // 2 - games // 4)


@unittest.skipUnless(ENGINE.exists(), "needs build/sensei")
class JudgeEngineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.engine = chess.engine.SimpleEngine.popen_uci(str(ENGINE))
        cls.an = Analyzer(cls.engine)

    @classmethod
    def tearDownClass(cls):
        cls.engine.quit()

    def test_book(self):
        v = judge(chess.Board(), chess.Move.from_uci("e2e4"), [stat("e2e4", "e4", 1000)], None, self.an)
        self.assertEqual(v.kind, "book")

    def test_mistake_hangs_queen(self):
        before = board_after("e4", "e5", "Qh5", "Nc6", "Bc4")
        self.assertNotEqual(judge(before, chess.Move.from_uci("g7g6"), None, None, self.an).kind, "mistake")
        v = judge(before, chess.Move.from_uci("g8f6"), None, None, self.an)
        self.assertEqual(v.kind, "mistake")
        self.assertTrue(v.consequence.startswith("it allows mate"), v.consequence)

    def test_mistake_loses_piece(self):
        before = board_after("e4", "e5", "Nf3", "Nc6")
        v = judge(before, chess.Move.from_uci("f1a6"), None, None, self.an)
        self.assertEqual(v.kind, "mistake")
        self.assertEqual(v.consequence, "it loses your bishop to 3...bxa6")

    def test_offbook_ok(self):
        v = judge(chess.Board(), chess.Move.from_uci("g1h3"), None, None, self.an)
        self.assertEqual(v.kind, "offbook")


class DescribeTest(unittest.TestCase):
    def test_describe_pawn(self):
        after = board_after("e4", "d5", "a3")
        self.assertEqual(describe(after, [chess.Move.from_uci("d5e4")], chess.WHITE), "it drops a pawn to 2...dxe4")

    def test_messages_fit(self):
        longest = max((o.name for o in OPENINGS), key=len)
        san = "exd8=Q+"
        v = Verdict("mistake", 900, None, f"it lets White take over with 12.{san}")
        trap = Trap(longest, [], san, san, "x" * 60)
        msgs = [
            msg_mistake(v, san, san, longest),
            msg_offbook(san, san, longest, "Nxe5"),
            msg_trap_warning(trap),
            msg_trap_setter(trap, "Black"),
            msg_gambit(longest, san),
            msg_summary(longest, 12, san),
            msg_summary(longest, None, None),
            msg_summary(longest, 12, None),
        ]
        self.assertNotIn("None", msg_summary("Italian Game", 5, None))
        for m in msgs:
            self.assertLessEqual(len(m), 140, m)
            self.assertNotIn("cp", m)
            self.assertIsNone(re.search(r"[+-]?\d+\.\d+\b", m.replace("...", " ")), m)


if __name__ == "__main__":
    unittest.main()
