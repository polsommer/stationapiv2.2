#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PREFIX="${STATIONAPI_CHAT_PREFIX:-/home/swg/chat}"
SERVICE_NAME="${STATIONAPI_CHAT_SERVICE_NAME:-stationchat}"
SESSION_NAME="${STATIONAPI_CHAT_TMUX_SESSION:-stationchat}"
RUN_USER="${STATIONAPI_CHAT_SERVICE_USER:-swg}"
WRAPPER_TARGET="${STATIONAPI_CHAT_WRAPPER_PATH:-/usr/local/bin/stationchat-tmux-wrapper}"
UNIT_PATH="${STATIONAPI_CHAT_SERVICE_UNIT:-/etc/systemd/system/${SERVICE_NAME}.service}"
TMUX_BIN="${STATIONAPI_CHAT_TMUX_BIN:-tmux}"

if command -v "$TMUX_BIN" >/dev/null 2>&1; then
  TMUX_BIN="$(command -v "$TMUX_BIN")"
fi

if [[ $EUID -ne 0 ]]; then
  echo "install_stationchat_service.sh must be run as root (try sudo)." >&2
  exit 1
fi

if [[ -z "$TMUX_BIN" ]]; then
  echo "install_stationchat_service.sh: tmux is required. Install it with 'sudo apt install tmux'." >&2
  exit 1
fi

if [[ ! -d "$PREFIX" ]]; then
  echo "install_stationchat_service.sh: install prefix '$PREFIX' does not exist. Build and install the chat gateway first." >&2
  exit 1
fi

if [[ ! -x "$PREFIX/stationchat" ]]; then
  echo "install_stationchat_service.sh: expected '${PREFIX}/stationchat' to exist. Did you run cmake --install or bootstrap_build.sh?" >&2
  exit 1
fi

install -Dm755 "$SCRIPT_DIR/stationchat_tmux_wrapper.sh" "$WRAPPER_TARGET"

cat >"$UNIT_PATH" <<EOF_UNIT
[Unit]
Description=SWG Station Chat Gateway (tmux-managed console)
After=network-online.target mariadb.service
Wants=network-online.target

[Service]
Type=simple
User=$RUN_USER
WorkingDirectory=$PREFIX
Environment=STATIONAPI_CHAT_PREFIX=$PREFIX
Environment=STATIONAPI_CHAT_TMUX_SESSION=$SESSION_NAME
Environment=STATIONAPI_CHAT_BIN=$PREFIX/stationchat
Environment=STATIONAPI_CHAT_TMUX_BIN=$TMUX_BIN
ExecStart=$WRAPPER_TARGET
ExecStop=/usr/bin/env bash -c '$TMUX_BIN send-keys -t $SESSION_NAME C-c >/dev/null 2>&1 || true'
ExecStopPost=/usr/bin/env bash -c '$TMUX_BIN kill-session -t $SESSION_NAME >/dev/null 2>&1 || true'
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF_UNIT

chmod 644 "$UNIT_PATH"

systemctl daemon-reload

cat <<EOF_MSG
Installed $UNIT_PATH
Wrapper copied to $WRAPPER_TARGET

Enable the service with:
  systemctl enable --now ${SERVICE_NAME}.service

Attach to the live console with:
  sudo -u $RUN_USER $TMUX_BIN attach -t $SESSION_NAME
EOF_MSG
