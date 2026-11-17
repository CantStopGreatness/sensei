"""lichess-bot engine class: Sensei as usual, plus opening coaching for beginners.

lichess-bot loads this through homemade.py (deploy/homemade.py). It is a UCIEngine running the
Sensei binary, so games that aren't coached go through lichess-bot's own book, tablebase,
pondering and draw/resign code unchanged.
"""
import copy
import datetime
import logging
import os
import time
from pathlib import Path

import chess
import chess.engine
import requests

from coach.explorer import Explorer
from coach.judge import Analyzer, fit
from coach.review import StudyClient, review_pgn
from coach.session import Session
from lib.config import Configuration
from lib.engine_wrapper import UCIEngine

log = logging.getLogger(__name__)

REPO = Path(__file__).resolve().parents[1]
SENSEI_ENGINE = os.environ.get("SENSEI_ENGINE", str(REPO / "build" / "sensei"))
START_WAIT = 15  # seconds to wait for !level / !opening before moving first as White
GREETING_START = "Hi! I'm Sensei"


def charge(limit: chess.engine.Limit, color: chess.Color, seconds: float) -> chess.engine.Limit:
    """The limit with `seconds` (time the coach already spent) taken off our clock."""
    limit = copy.copy(limit)
    if color == chess.WHITE and limit.white_clock is not None:
        limit.white_clock = max(0.1, limit.white_clock - seconds)
    if color == chess.BLACK and limit.black_clock is not None:
        limit.black_clock = max(0.1, limit.black_clock - seconds)
    if limit.time is not None:
        limit.time = max(0.5, limit.time - seconds)
    return limit


def is_coaching_game(game) -> bool:
    """Human opponent, casual, rapid or slower (initial + 40 * increment >= 10 min), normal start."""
    estimated = game.clock_initial + 40 * game.clock_increment
    return (not game.opponent.is_bot
            and game.mode == "casual"
            and estimated >= datetime.timedelta(minutes=10)
            and game.variant_name == "Standard"
            and game.initial_fen in (None, "", "startpos"))


class ChatClient:
    """The game's player chat through the Lichess bot API. Never raises; never logs the token."""

    def __init__(self, game_id: str, token: str, session: requests.Session | None = None,
                 base_url: str = "https://lichess.org/"):
        self.url = f"{base_url.rstrip('/')}/api/bot/game/{game_id}/chat"
        self.session = session or requests.Session()
        self.headers = {"Authorization": f"Bearer {token}"}

    def fetch(self) -> list[tuple[str, str]]:
        try:
            r = self.session.get(self.url, headers=self.headers, timeout=2)
            if r.status_code != 200:
                return []
            return [(m.get("user", ""), m.get("text", "")) for m in r.json()]
        except Exception as e:
            log.info("chat fetch failed: %s", type(e).__name__)
            return []

    def post(self, text: str) -> None:
        try:
            self.session.post(self.url, headers=self.headers, data={"room": "player", "text": text}, timeout=2)
        except Exception as e:
            log.info("chat post failed: %s", type(e).__name__)


def _without_online_moves(engine_cfg: Configuration) -> Configuration:
    """The engine config with every online book and tablebase source turned off."""
    cfg = copy.deepcopy(engine_cfg.config)
    for source in (cfg.get("online_moves") or {}).values():
        if isinstance(source, dict):
            source["enabled"] = False
    return Configuration(cfg)


class SenseiCoach(UCIEngine):
    def __init__(self, commands, options, stderr, draw_or_resign, game, debug, **popen_args):
        super().__init__([SENSEI_ENGINE], options, stderr, draw_or_resign, game, debug, **popen_args)
        self.coaching = game is not None and is_coaching_game(game)
        if self.coaching:
            color = chess.WHITE if game.is_white else chess.BLACK
            self.opponent = (game.opponent.name or "").lower()
            self.me = (game.me.name or "").lower()
            self.session = Session(color, Explorer(), Analyzer(self.engine))
            token = os.environ.get("LICHESS_BOT_TOKEN", "")
            self.chat = ChatClient(game.id, token, base_url=game.base_url)
            self.studies = StudyClient(token, base_url=game.base_url)
            self.reviewed = False
            self.chat_seen = None  # chat lines already read; None until the first read
            self.greeted = False
            log.info("Coaching game %s against %s", game.id, game.opponent.name)

    def play_move(self, board, game, li, setup_timer, move_overhead, can_ponder, is_correspondence,
                  correspondence_move_time, engine_cfg, min_time):
        if self.coaching:  # the coach picks book moves itself; no pondering, the engine is shared
            engine_cfg, can_ponder = _without_online_moves(engine_cfg), False
        super().play_move(board, game, li, setup_timer, move_overhead, can_ponder, is_correspondence,
                          correspondence_move_time, engine_cfg, min_time)

    def search(self, board, time_limit, ponder, draw_offered, root_moves):
        if not self.coaching:
            return super().search(board, time_limit, ponder, draw_offered, root_moves)

        started = time.monotonic()
        if not self.greeted:
            self.greeted = True
            history = self.chat.fetch()
            # A restart mid-game: restore earlier commands quietly; don't greet or wait again.
            resumed = len(board.move_stack) >= 2 or any(
                u.lower() == self.me and t.startswith(GREETING_START) for u, t in history)
            if not resumed:
                self.chat.post(self.session.greeting())
            got_command = self._read_chat(replay=not resumed, lines=history)
            if not resumed:
                if not board.move_stack and board.turn == self.session.color:
                    deadline = time.monotonic() + START_WAIT
                    while not got_command and time.monotonic() < deadline:
                        time.sleep(1)
                        got_command = self._read_chat()
        else:
            self._read_chat()

        decision = self.session.decide(board)
        for text in decision.messages:
            self.chat.post(text)
        if decision.move is not None:
            self.scores.append(chess.engine.PovScore(chess.engine.Mate(1), board.turn))  # neutral for draw/resign
            return chess.engine.PlayResult(decision.move, None)
        time_limit = charge(time_limit, board.turn, time.monotonic() - started)
        level = self.session.level
        if decision.full_strength or level is None:
            self.engine.configure({"UCI_LimitStrength": False})
        else:
            self.engine.configure({"UCI_LimitStrength": True, "UCI_Elo": level})
        return super().search(board, time_limit, False, draw_offered, root_moves)

    def send_game_result(self, game, board) -> None:
        """At the end of a coached game, post a study of it with every comment and arrow."""
        super().send_game_result(game, board)
        if not self.coaching or self.reviewed or not self.session.moments:
            return
        self.reviewed = True  # lichess-bot can report the result more than once
        white, black = game.white.name, game.black.name
        pgn = review_pgn(board, self.session.moments, white, black)
        url = self.studies.publish(f"Sensei review: {white} vs {black}", pgn, not self.session.color)
        if url:
            self.chat.post(fit(f"Your opening review, with arrows: {url}"))

    def _read_chat(self, replay: bool = True, lines: list[tuple[str, str]] | None = None) -> bool:
        """Apply the opponent's new chat commands and post the replies (unless `replay` is
        False, after a restart). True if any reply was posted."""
        lines = self.chat.fetch() if lines is None else lines
        seen = self.chat_seen or 0
        new, self.chat_seen = lines[seen:], max(seen, len(lines))
        handled = False
        for user, text in new:
            if user.lower() != self.opponent:
                continue
            reply = self.session.command(text)
            if reply and replay:
                self.chat.post(reply)
                handled = True
        return handled
