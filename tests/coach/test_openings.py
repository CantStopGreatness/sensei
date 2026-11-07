import unittest

import chess

from coach.openings import family, find, name_for


def board_after(*sans):
    b = chess.Board()
    for s in sans:
        b.push_san(s)
    return b


class OpeningsTest(unittest.TestCase):
    def test_name_for(self):
        self.assertEqual(name_for(board_after("e4", "e5", "Nf3", "Nc6", "Bc4")), "Italian Game")
        self.assertIsNone(name_for(board_after("a3", "h6", "h3", "a6", "Ra2", "Rh7")))

    def test_find(self):
        found = find("italian")
        self.assertTrue(found)
        self.assertTrue(all("Italian" in o.name for o in found))
        self.assertEqual([m.uci() for m in find("ruy lopez")[0].moves[:5]],
                         ["e2e4", "e7e5", "g1f3", "b8c6", "f1b5"])
        self.assertEqual(find("xyzzy"), [])

    def test_family(self):
        najdorf = next(o for o in find("najdorf") if o.name.startswith("Sicilian Defense: Najdorf"))
        self.assertEqual(family(najdorf), "Sicilian Defense")


if __name__ == "__main__":
    unittest.main()
