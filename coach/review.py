"""The end-of-game review: the game as a Lichess study with Sensei's comments and arrows."""
import logging
import re

import chess
import chess.pgn
import chess.svg
import requests

log = logging.getLogger(__name__)


def review_pgn(board: chess.Board, moments, white: str, black: str) -> str:
    """The game as PGN. Each moment's words go on the move it was about; its arrows go on the
    position before that move, where the choice was made."""
    game = chess.pgn.Game()
    game.headers.update(Event="Sensei opening review", White=white, Black=black)
    nodes = [game]
    for move in board.move_stack:
        nodes.append(nodes[-1].add_variation(move))
    for m in moments:
        if not 0 < m.ply < len(nodes):  # e.g. said before a takeback
            continue
        node = nodes[m.ply]
        node.comment = f"{node.comment} {m.text}".strip()
        before = nodes[m.ply - 1]
        before.set_arrows(before.arrows() + [chess.svg.Arrow(a, b, color=c) for a, b, c in m.arrows])
    return str(game)


class StudyClient:
    """Creates review studies through the Lichess API. Never raises; never logs the token.
    Needs the study:write scope: without it publish() just returns None."""

    def __init__(self, token: str, session: requests.Session | None = None, base_url: str = "https://lichess.org/"):
        self.base = base_url.rstrip("/")
        self.session = session or requests.Session()
        self.headers = {"Authorization": f"Bearer {token}"}

    def _call(self, method: str, path: str, **kwargs):
        r = self.session.request(method, self.base + path, headers=self.headers, timeout=5, **kwargs)
        if r.status_code >= 300:
            raise RuntimeError(f"{method} {path.split('/')[2]} -> HTTP {r.status_code}")
        return r

    def publish(self, name: str, pgn: str, orientation: chess.Color) -> str | None:
        """An unlisted study holding the game; returns the chapter's URL."""
        try:
            sid = self._call("POST", "/api/study", data={
                "name": name[:100], "visibility": "unlisted", "computer": "everyone", "explorer": "everyone",
                "cloneable": "everyone", "shareable": "everyone", "chat": "member"}).json()["id"]
            chapters = self._call("POST", f"/api/study/{sid}/import-pgn", data={
                "pgn": pgn, "name": "Review", "orientation": "white" if orientation == chess.WHITE else "black",
            }).json()["chapters"]
        except Exception as e:
            log.info("review study not created: %s", e if isinstance(e, RuntimeError) else type(e).__name__)
            return None
        url = f"{self.base}/study/{sid}/{chapters[0]['id']}"
        try:  # a new study starts with an empty "Chapter 1": remove it so the review opens first
            export = self._call("GET", f"/api/study/{sid}.pgn").text
            first = re.search(r'\[ChapterURL "[^"]*/([^/"]+)"\]', export)
            if first and first.group(1) != chapters[0]["id"]:
                self._call("DELETE", f"/api/study/{sid}/{first.group(1)}")
        except Exception as e:
            log.info("empty study chapter left in place: %s", type(e).__name__)
        return url
