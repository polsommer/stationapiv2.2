#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
UDP_DIR="${REPO_ROOT}/externals/udplibrary"
INSTALL_PREFIX_DEFAULT="/home/swg/chat"
INSTALL_PREFIX="${STATIONAPI_CHAT_PREFIX:-${INSTALL_PREFIX_DEFAULT}}"

if [[ ! -d "${UDP_DIR}" ]]; then
  echo "Cloning udplibrary into externals/udplibrary..."
  git clone https://bitbucket.org/swgathrawn/udplibrary.git "${UDP_DIR}"
else
  echo "udplibrary already present at externals/udplibrary; skipping clone."
fi

BUILD_DIR="${REPO_ROOT}/build"
mkdir -p "${BUILD_DIR}"

pushd "${BUILD_DIR}" >/dev/null
cmake ..
cmake --build .
cmake --install . --prefix "${INSTALL_PREFIX}"
popd >/dev/null

echo "Installed chat runtime to ${INSTALL_PREFIX}"
