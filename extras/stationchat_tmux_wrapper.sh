#!/usr/bin/env bash
set -euo pipefail

SESSION="${STATIONAPI_CHAT_TMUX_SESSION:-stationchat}"
PREFIX="${STATIONAPI_CHAT_PREFIX:-/home/swg/chat}"
CHAT_BIN="${STATIONAPI_CHAT_BIN:-${PREFIX}/stationchat}"
TMUX_BIN="${STATIONAPI_CHAT_TMUX_BIN:-tmux}"

if ! command -v "$TMUX_BIN" >/dev/null 2>&1; then
  echo "stationchat_tmux_wrapper: tmux is required but was not found in PATH" >&2
  exit 1
fi

if [[ ! -x "$CHAT_BIN" ]]; then
  echo "stationchat_tmux_wrapper: expected runnable stationchat binary at '$CHAT_BIN'" >&2
  exit 1
fi

# Create a temporary directory to capture the exit code of the chat process.
STATE_DIR=$(mktemp -d)
trap 'rm -rf "$STATE_DIR"' EXIT
STATUS_FILE="$STATE_DIR/status"

printf -v START_COMMAND 'cd %q && exec %q' "$PREFIX" "$CHAT_BIN"

# If a previous session exists, terminate it before starting a new one.
if "$TMUX_BIN" has-session -t "$SESSION" 2>/dev/null; then
  "$TMUX_BIN" send-keys -t "$SESSION" C-c >/dev/null 2>&1 || true
  sleep 1
  "$TMUX_BIN" kill-session -t "$SESSION" >/dev/null 2>&1 || true
fi

"$TMUX_BIN" new-session -d -s "$SESSION" "${START_COMMAND}; EXIT_CODE=\$?; echo \$EXIT_CODE > '$STATUS_FILE';" >/dev/null

# Wait until the session terminates and the exit status is written.
while "$TMUX_BIN" has-session -t "$SESSION" 2>/dev/null; do
  sleep 2
  if [[ -s "$STATUS_FILE" ]]; then
    break
  fi
done

if [[ -s "$STATUS_FILE" ]]; then
  read -r EXIT_CODE < "$STATUS_FILE"
else
  EXIT_CODE=0
fi

exit "$EXIT_CODE"
