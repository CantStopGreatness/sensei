#!/bin/sh
# Started by launchd (~/Library/LaunchAgents/com.aryanthakur.sensei-lichess-bot.plist).
LICHESS_BOT_TOKEN="$(cat "$HOME/.config/lichess-bot/token")"
export LICHESS_BOT_TOKEN
cd "$HOME/lichess-bot" || exit 1
exec venv/bin/python lichess-bot.py
