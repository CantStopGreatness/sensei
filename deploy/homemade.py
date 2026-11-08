"""lichess-bot's homemade.py, symlinked from ~/lichess-bot/homemade.py.

lichess-bot imports engine classes from this module by the name in config.yml (engine.name). It
puts the Sensei release clone on sys.path and re-exports the coaching engine from coach/.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]  # follows the symlink to the sensei clone
if str(REPO) not in sys.path:
    sys.path.insert(0, str(REPO))

from coach.lichess_engine import SenseiCoach  # noqa: E402,F401
from lib.engine_wrapper import MinimalEngine  # noqa: E402


class ExampleEngine(MinimalEngine):
    """lichess-bot's test_bot/homemade.py imports this name from homemade; it is never played."""
