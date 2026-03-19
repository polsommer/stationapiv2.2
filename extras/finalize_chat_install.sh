#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: finalize_chat_install.sh [options] <install_prefix> [run_link]

Copies default configuration files, writes the stationchat launcher, and
optionally creates/updates a symlink for easier startup.

Options:
  -d, --debug   Enable verbose debug output (sets shell xtrace).
  --no-color    Disable coloured status messages.
  -h, --help    Show this help message and exit.
EOF
}

DEBUG=0
USE_COLOR=1

while (($#)); do
  case "$1" in
    -d|--debug)
      DEBUG=1
      shift
      ;;
    --no-color)
      USE_COLOR=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      echo "finalize_chat_install.sh: unknown option '$1'" >&2
      usage >&2
      exit 1
      ;;
    *)
      break
      ;;
  esac
done

if (( DEBUG )); then
  set -x
fi

if [[ ! -t 1 ]] || (( USE_COLOR == 0 )); then
  C_INFO=""
  C_WARN=""
  C_ERROR=""
  C_DEBUG=""
  C_RESET=""
else
  C_INFO="\033[1;34m"
  C_WARN="\033[1;33m"
  C_ERROR="\033[1;31m"
  C_DEBUG="\033[1;35m"
  C_RESET="\033[0m"
fi

log_info() {
  echo -e "${C_INFO}==>${C_RESET} $*"
}

log_warn() {
  echo -e "${C_WARN}==> WARNING:${C_RESET} $*" >&2
}

log_error() {
  echo -e "${C_ERROR}==> ERROR:${C_RESET} $*" >&2
}

log_debug() {
  if (( DEBUG )); then
    echo -e "${C_DEBUG}[DEBUG]${C_RESET} $*"
  fi
}

INSTALL_PREFIX="${1:?Provide an install prefix}"
RUN_LINK="${2:-/chat}"

log_info "Finalising installation at ${INSTALL_PREFIX}"
log_info "Run link target: ${RUN_LINK:-'(none)'}"

STATIONCHAT_BIN="${INSTALL_PREFIX}/bin/stationchat"
if [[ ! -x "${STATIONCHAT_BIN}" ]]; then
  log_error "${STATIONCHAT_BIN} not found or not executable. Ensure cmake --install completed successfully."
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
log_debug "Script directory: ${SCRIPT_DIR}"

CONFIG_SOURCE_DIR="${SCRIPT_DIR}"
CONFIG_TARGET_DIR="${INSTALL_PREFIX}/etc/stationapi"
log_info "Ensuring configuration directory ${CONFIG_TARGET_DIR} exists"
mkdir -p "${CONFIG_TARGET_DIR}"
for cfg in swgchat logger; do
  TARGET_PATH="${CONFIG_TARGET_DIR}/${cfg}.cfg"
  if [[ ! -f "${TARGET_PATH}" ]]; then
    DIST_PATH="${CONFIG_SOURCE_DIR}/${cfg}.cfg.dist"
    if [[ -f "${DIST_PATH}" ]]; then
      log_info "Copying default ${cfg}.cfg"
      cp "${DIST_PATH}" "${TARGET_PATH}"
    else
      log_warn "Missing ${TARGET_PATH} and unable to locate ${DIST_PATH}."
    fi
  else
    log_debug "${TARGET_PATH} already exists; leaving in place"
  fi
done

WRAPPER_PATH="${INSTALL_PREFIX}/stationchat"
log_info "Writing launcher script to ${WRAPPER_PATH}"
cat <<'WRAP' >"${WRAPPER_PATH}"
#!/usr/bin/env bash
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${DIR}/bin/stationchat"

if [[ -t 1 ]]; then
  C_INFO="\033[1;34m"
  C_ERROR="\033[1;31m"
  C_RESET="\033[0m"
else
  C_INFO=""
  C_ERROR=""
  C_RESET=""
fi

if [[ ! -x "${BIN}" ]]; then
  printf '%s==> ERROR:%s Expected executable %s but it was not found. Did installation finish?\n' "${C_ERROR}" "${C_RESET}" "${BIN}" >&2
  exit 1
fi

printf '%s==>%s Launching stationchat (%s)\n' "${C_INFO}" "${C_RESET}" "${BIN}"
cd "${DIR}"
exec "${BIN}" "$@"
WRAP
chmod +x "${WRAPPER_PATH}"

RUN_LOCATION="${INSTALL_PREFIX}"
if [[ -n "${RUN_LINK}" ]]; then
  if [[ -e "${RUN_LINK}" && ! -L "${RUN_LINK}" ]]; then
    log_warn "${RUN_LINK} exists and is not a symlink; skipping creation of chat run link."
  else
    if ln -sfn "${INSTALL_PREFIX}" "${RUN_LINK}"; then
      RUN_LOCATION="${RUN_LINK}"
      log_info "Linked ${RUN_LINK} -> ${INSTALL_PREFIX}"
    else
      log_warn "Unable to create ${RUN_LINK} -> ${INSTALL_PREFIX}. Create the link manually if desired."
    fi
  fi
else
  log_debug "Run link disabled; skipping symlink creation"
fi

log_info "Launcher installed to ${WRAPPER_PATH}"
log_info "Start the chat server with:"
echo "  cd ${RUN_LOCATION}"
echo "  ./stationchat"
