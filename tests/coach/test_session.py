import unittest

import chess

from coach.explorer import MoveStat
from coach.session import Decision, Session


def stat(uci, san, games):
    return MoveStat(uci, san, games, games // 2, games // 4, games - games // 2 - games // 4)


def board_after(*sans):
    b = chess.Board()
    for s in sans:
        b.push_san(s)
    return b


class FakeExplorer:
    def __init__(self, masters=None, human=None, default_masters=None):
        self.m, self.h, self.default = masters or {}, human or {}, default_masters

    def masters(self, board):
        return self.m.get(board.epd(), self.default)

    def lichess(self, board, rating):
        return self.h.get(board.epd())


class FakeAnalyzer:
    """Every position scores `each` for the side to move, so any move loses 2 * each."""

    def __init__(self, each=0, fail=False):
        self.each, self.fail = each, fail

    def score(self, board):
        if self.fail:
            raise RuntimeError("engine died")
        return self.each

    def refutation(self, board):
        if self.fail:
            raise RuntimeError("engine died")
        return []


class SessionTest(unittest.TestCase):
    def test_command_parsing(self):
        s = Session(chess.WHITE, FakeExplorer(), FakeAnalyzer())
        replies = []
        replies.append(s.command("!level 800"))
        self.assertEqual(s.level, 800)
        self.assertEqual(replies[-1], "Level set to 800.")
        s.level = 1200
        replies.append(s.command("  !LEVEL 800 "))
        self.assertEqual(s.level, 800)
        replies.append(s.command("!level 99999"))
        self.assertEqual(s.level, 2400)
        self.assertEqual(replies[-1], "Level set to 2400.")
        replies.append(s.command("!level abc"))
        self.assertEqual(replies[-1], "Use !level 400-2400 or !level max.")
        self.assertEqual(s.level, 2400)
        replies.append(s.command("!level max"))
        self.assertIsNone(s.level)
        self.assertEqual(replies[-1], "Level set to max.")
        replies.append(s.command("!opening xyzzy"))
        self.assertEqual(replies[-1], "Couldn't find that opening. Try e.g. !opening sicilian.")
        replies.append(s.command("!opening italian"))
        self.assertEqual(replies[-1], "Practising the Italian Game. I'll play the other side.")
        self.assertIsNone(s.command("hello"))
        self.assertIsNone(s.command("!wait"))  # lichess-bot's own command
        replies.append(s.greeting())
        for r in replies:
            self.assertLessEqual(len(r), 140, r)
        self.assertIn("!level", s.greeting())
        self.assertIn("!opening", s.greeting())

    def test_plays_main_line(self):
        ex = FakeExplorer({chess.Board().epd(): [stat("e2e4", "e4", 1000), stat("d2d4", "d4", 900)]})
        d = Session(chess.WHITE, ex, FakeAnalyzer()).decide(chess.Board())
        self.assertEqual(d.move, chess.Move.from_uci("e2e4"))
        self.assertEqual(d.messages, [])

    def test_opening_choice_steers(self):
        b = board_after("e4", "e5", "Nf3", "Nc6")
        ex = FakeExplorer({b.epd(): [stat("d2d4", "d4", 5000)]})
        s = Session(chess.WHITE, ex, FakeAnalyzer())
        s.command("!opening italian")
        s.decide(chess.Board())
        self.assertEqual(s.decide(b).move, chess.Move.from_uci("f1c4"))

    def test_mistake_is_punished_and_explained(self):
        s = Session(chess.BLACK, FakeExplorer(), FakeAnalyzer(each=150))
        d = s.decide(board_after("e4"))
        self.assertEqual(len(d.messages), 1)
        self.assertTrue(d.messages[0].startswith("1.e4 was a mistake"), d.messages)
        self.assertIsNone(d.move)
        self.assertTrue(d.full_strength)

    def test_one_message_per_move(self):
        setup = "e4 e5 Nf3 d6 Bc4 Bg4 Nc3 g6 Nxe5".split()  # Légal's mate, Sensei (Black) is the victim
        b = board_after(*setup)
        before = b.copy()
        before.pop()
        ex = FakeExplorer({before.epd(): [stat("h2h3", "h3", 500)]})
        d = Session(chess.BLACK, ex, FakeAnalyzer()).decide(board_after(*setup[:1]))
        s = Session(chess.BLACK, ex, FakeAnalyzer())
        s.decide(board_after(*setup[:1]))
        d = s.decide(b)
        self.assertEqual(len(d.messages), 1, d.messages)
        self.assertTrue(d.messages[0].startswith("Trap set!"), d.messages)

    def test_explorer_failure_is_silent(self):
        s = Session(chess.BLACK, FakeExplorer(), FakeAnalyzer(fail=True))
        d = s.decide(board_after("e4"))
        self.assertEqual(d, Decision(None, True, []))

    def test_takeback_and_resume(self):
        s = Session(chess.WHITE, FakeExplorer(), FakeAnalyzer(each=150))
        plies = ["e4", "e5", "Nf3", "Nc6", "Bc4", "Nf6"]
        said = []
        for n in (2, 4, 6, 4, 6):
            said.append(s.decide(board_after(*plies[:n])).messages)
        self.assertEqual([len(m) for m in said], [1, 1, 1, 0, 0])

        resumed = Session(chess.WHITE, FakeExplorer(), FakeAnalyzer(each=150))
        twenty = board_after(*("Nf3 Nf6 Ng1 Ng8 " * 5).split())
        self.assertEqual(resumed.decide(twenty).messages, [])
        self.assertFalse(resumed.in_opening)

    def test_opening_ends_by_move_15(self):
        lots = [stat("a2a3", "a3", 1000)]
        s = Session(chess.WHITE, FakeExplorer(default_masters=lots), FakeAnalyzer())
        s.decide(chess.Board())
        shuffle = ("Nf3 Nf6 Ng1 Ng8 " * 8).split()  # 32 plies: move 17
        d = s.decide(board_after(*shuffle))
        self.assertEqual(sum(m.startswith("Opening over") for m in d.messages), 1, d.messages)
        self.assertFalse(s.in_opening)
        d = s.decide(board_after(*shuffle, "Nf3", "Nf6"))
        self.assertEqual(d.messages, [])

    def test_level_after_opening(self):
        twenty = board_after(*("Nf3 Nf6 Ng1 Ng8 " * 5).split())
        s = Session(chess.WHITE, FakeExplorer(), FakeAnalyzer(), level=800)
        self.assertFalse(s.decide(twenty).full_strength)
        s = Session(chess.WHITE, FakeExplorer(), FakeAnalyzer(), level=None)
        self.assertTrue(s.decide(twenty).full_strength)

    def test_gambit_message_only_for_beginners_gambit_once(self):
        kg = board_after("e4", "e5", "f4")
        ex = FakeExplorer({kg.copy(stack=False).epd(): [stat("e5f4", "exf4", 1000)],
                           board_after("e4", "e5").epd(): [stat("f2f4", "f4", 1000)]})
        s = Session(chess.BLACK, ex, FakeAnalyzer())
        s.decide(board_after("e4"))
        d = s.decide(kg)
        self.assertEqual(d.messages, ["That's the King's Gambit. A strong reply is 2...exf4."])

        qgd = board_after("d4", "d5", "c4", "e6")
        ex = FakeExplorer({board_after("d4", "d5", "c4").epd(): [stat("e7e6", "e6", 1000)],
                           qgd.epd(): [stat("b1c3", "Nc3", 1000)]})
        s = Session(chess.WHITE, ex, FakeAnalyzer())
        s.decide(board_after("d4", "d5"))
        self.assertEqual(s.decide(qgd).messages, [])

    def test_never_plays_trap_bait_in_gambit(self):
        b = board_after("e4", "e5", "f4", "Bc5")  # Falkbeer ...Bc5 trap: fxe5?? Qh4+
        ex = FakeExplorer({b.epd(): [MoveStat("f4e5", "fxe5", 100, 90, 5, 5), MoveStat("g1f3", "Nf3", 100, 40, 30, 30)]})
        s = Session(chess.WHITE, ex, FakeAnalyzer())
        s.decide(board_after("e4", "e5"))
        self.assertEqual(s.decide(b).move, chess.Move.from_uci("g1f3"))

    def test_engine_move_is_not_called_book(self):
        s = Session(chess.WHITE, FakeExplorer(), FakeAnalyzer(each=150))
        s.analyzer.refutation = lambda board: [chess.Move.from_uci("g8f6")] if board.turn == chess.BLACK else []
        d = s.decide(board_after("e4", "f6"))
        self.assertEqual(len(d.messages), 2, d.messages)  # the text, then the board link
        self.assertNotIn("Book", d.messages[0])
        self.assertNotIn("None", d.messages[0])
        self.assertIn("Better was Nf6 (g8→f6).", d.messages[0])
        self.assertIn("lastMove=g8f6&color=black", d.messages[1])

    def test_offbook_names_good_moves_and_links_the_top_one(self):
        before = board_after("e4", "e5", "Nf3", "Nc6")
        ex = FakeExplorer({before.epd(): [stat("f1b5", "Bb5", 900), stat("f1c4", "Bc4", 800),
                                          stat("d2d4", "d4", 600), stat("b1c3", "Nc3", 500)]})
        s = Session(chess.BLACK, ex, FakeAnalyzer())
        s.decide(board_after("e4"))
        d = s.decide(board_after("e4", "e5", "Nf3", "Nc6", "a3"))
        self.assertEqual(len(d.messages), 2, d.messages)
        self.assertIn("Bb5 (f1→b5), Bc4 (f1→c4), d4 (d2→d4)", d.messages[0])
        self.assertNotIn("Nc3", d.messages[0])
        self.assertEqual(d.messages[1], "https://lichess1.org/export/fen.gif?fen="
                         "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R&lastMove=f1b5&color=white")
        # Kept for the end-of-game review: the words (no link) and arrows for the good moves.
        self.assertEqual(len(s.moments), 1)
        m = s.moments[0]
        self.assertEqual((m.ply, m.text), (5, d.messages[0]))
        self.assertEqual(m.arrows, [(chess.F1, chess.B5, "green"), (chess.F1, chess.C4, "green"),
                                    (chess.D2, chess.D4, "green"), (chess.A2, chess.A3, "blue")])


if __name__ == "__main__":
    unittest.main()
