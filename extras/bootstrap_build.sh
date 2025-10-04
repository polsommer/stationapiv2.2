#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
UDP_DIR="${REPO_ROOT}/externals/udplibrary"

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
make
popd >/dev/null
