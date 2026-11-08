import re
import unittest
from pathlib import Path

import chess
import chess.engine

from coach.explorer import MoveStat
from coach.judge import (Analyzer, Verdict, board_link, describe, good_moves, judge, msg_gambit, msg_mistake,
                         msg_offbook, msg_summary, msg_trap_setter, msg_trap_warning, with_squares)
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
            msg_mistake(v, san, ["Nxe5 (f3→e5)"] * 3, True),
            msg_mistake(v, san, ["Nxe5 (f3→e5)"], False),
            msg_offbook(san, ["Nxe5 (f3→e5)"] * 3, longest, "Nxe5"),
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


class ScoreTable:
    """Analyzer stand-in: the side to move's score is looked up by position, default 0."""

    def __init__(self, scores, pv=()):
        self.scores, self.pv = scores, list(pv)

    def score(self, board):
        return self.scores.get(board.epd(), 0)

    def refutation(self, board):
        return self.pv


class GoodMovesTest(unittest.TestCase):
    def test_book_moves_within_50cp_most_popular_first(self):
        before = board_after("e4", "e5", "Nf3", "Nc6")
        masters = [stat("f1b5", "Bb5", 900), stat("f1c4", "Bc4", 800), stat("f1a6", "Ba6", 700),
                   stat("d2d4", "d4", 600), stat("b1c3", "Nc3", 500), stat("c2c3", "c3", 5)]
        bad = before.copy()
        bad.push(chess.Move.from_uci("f1a6"))
        an = ScoreTable({before.epd(): 30, bad.epd(): 300})  # Ba6 loses 330, the rest lose 30
        moves, from_book = good_moves(before, masters, an)
        self.assertEqual([m.uci() for m in moves], ["f1b5", "f1c4", "d2d4"])
        self.assertTrue(from_book)

    def test_falls_back_to_engine_best(self):
        before = board_after("e4")
        moves, from_book = good_moves(before, None, ScoreTable({}, pv=[chess.Move.from_uci("e7e5")]))
        self.assertEqual(moves, [chess.Move.from_uci("e7e5")])
        self.assertFalse(from_book)
        self.assertEqual(good_moves(before, None, ScoreTable({})), ([], False))

    def test_with_squares(self):
        b = board_after("e4", "e5", "Nf3", "Nc6")
        self.assertEqual(with_squares(b, chess.Move.from_uci("f1b5")), "Bb5 (f1→b5)")
        self.assertEqual(with_squares(chess.Board(), chess.Move.from_uci("e2e4")), "e4 (e2→e4)")

    def test_board_link(self):
        b = board_after("e4", "e5", "Nf3", "Nc6")
        self.assertEqual(board_link(b, chess.Move.from_uci("f1b5"), chess.WHITE),
                         "https://lichess1.org/export/fen.gif?fen=r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R"
                         "&lastMove=f1b5&color=white")
        widest = chess.Board("1p1p1p1p/p1p1p1p1/1p1p1p1p/p1p1p1p1/1P1P1P1P/P1P1P1P1/1P1P1P1P/P1P1P1PK w - - 0 1")
        self.assertLessEqual(len(board_link(widest, chess.Move.from_uci("a8a7"), chess.BLACK)), 140)


if __name__ == "__main__":
    unittest.main()
