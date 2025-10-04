#!/usr/bin/env bash
set -euo pipefail

INSTALL_PREFIX="${1:?Provide an install prefix}"
RUN_LINK="${2:-/chat}"

STATIONCHAT_BIN="${INSTALL_PREFIX}/bin/stationchat"
if [[ ! -x "${STATIONCHAT_BIN}" ]]; then
  echo "Error: ${STATIONCHAT_BIN} not found or not executable. Ensure cmake --install completed successfully." >&2
  exit 1
fi

WRAPPER_PATH="${INSTALL_PREFIX}/stationchat"
cat <<'WRAP' >"${WRAPPER_PATH}"
#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${DIR}/bin/stationchat" "$@"
WRAP
chmod +x "${WRAPPER_PATH}"

RUN_LOCATION="${INSTALL_PREFIX}"
if [[ -n "${RUN_LINK}" ]]; then
  if [[ -e "${RUN_LINK}" && ! -L "${RUN_LINK}" ]]; then
    echo "Warning: ${RUN_LINK} exists and is not a symlink; skipping creation of chat run link." >&2
  else
    if ln -sfn "${INSTALL_PREFIX}" "${RUN_LINK}"; then
      RUN_LOCATION="${RUN_LINK}"
      echo "Linked ${RUN_LINK} -> ${INSTALL_PREFIX}"
    else
      echo "Warning: unable to create ${RUN_LINK} -> ${INSTALL_PREFIX}. Create the link manually if desired." >&2
    fi
  fi
fi

echo "Launcher installed to ${WRAPPER_PATH}"  
echo "Start the chat server with:"  
echo "  cd ${RUN_LOCATION}"  
echo "  ./stationchat"
