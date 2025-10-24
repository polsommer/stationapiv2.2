#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: bootstrap_build.sh [options]

Ensures the UDP library dependency is present, configures, builds, and installs
the chat gateway, and finalises the runtime layout.

Options:
  -d, --debug       Enable verbose debug output (sets shell xtrace).
  --no-color        Disable coloured status messages.
  -h, --help        Show this help message and exit.
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
      echo "bootstrap_build.sh: unknown option '$1'" >&2
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

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    log_error "Required command '$1' not found in PATH. Install it and re-run the bootstrap."
    exit 1
  fi
}

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
UDP_DIR="${REPO_ROOT}/externals/udplibrary"
INSTALL_PREFIX_DEFAULT="/home/swg/chat"
INSTALL_PREFIX="${STATIONAPI_CHAT_PREFIX:-${INSTALL_PREFIX_DEFAULT}}"
RUN_LINK_DEFAULT="/chat"
RUN_LINK="${STATIONAPI_CHAT_RUNLINK:-${RUN_LINK_DEFAULT}}"

log_debug "Resolved repository root: ${REPO_ROOT}"
log_info "Using install prefix: ${INSTALL_PREFIX}"
log_info "Run link target: ${RUN_LINK:-'(none)'}"

require_command git
require_command cmake

if [[ ! -d "${UDP_DIR}" ]]; then
  log_info "Cloning udplibrary into externals/udplibrary"
  git clone https://bitbucket.org/swgathrawn/udplibrary.git "${UDP_DIR}"
else
  log_debug "udplibrary already present at ${UDP_DIR}"
  log_info "Skipping udplibrary clone"
fi

BUILD_DIR="${REPO_ROOT}/build"
log_debug "Build directory: ${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

pushd "${BUILD_DIR}" >/dev/null
log_info "Configuring CMake project"
cmake ..

log_info "Building project"
cmake --build .

log_info "Installing to ${INSTALL_PREFIX}"
cmake --install . --prefix "${INSTALL_PREFIX}"
popd >/dev/null

FINALIZE_ARGS=()
if (( DEBUG )); then
  FINALIZE_ARGS+=(--debug)
fi
FINALIZE_ARGS+=("${INSTALL_PREFIX}")
FINALIZE_ARGS+=("${RUN_LINK}")

log_info "Finalising installation layout"
"${REPO_ROOT}/extras/finalize_chat_install.sh" "${FINALIZE_ARGS[@]}"

log_info "Bootstrap completed successfully."
