#!/usr/bin/env bash
set -euo pipefail

WORKFLOW_PRESET="${WORKFLOW_PRESET:-cp0-cross-package}"
REMOTE_USER="${REMOTE_USER:-}"
REMOTE_HOST="${REMOTE_HOST:-}"
REMOTE_DIR="${REMOTE_DIR:-/home/${REMOTE_USER}}"
PACKAGE_GLOB="${PACKAGE_GLOB:-dist/kvxputerzero_*_arm64.deb}"

log() {
  printf '\n==> %s\n' "$*"
}

log "Workflow: cmake --workflow --preset ${WORKFLOW_PRESET}"
cmake --workflow --preset "${WORKFLOW_PRESET}"

shopt -s nullglob
packages=( ${PACKAGE_GLOB} )
shopt -u nullglob

if (( ${#packages[@]} == 0 )); then
  printf 'No package matched: %s\n' "${PACKAGE_GLOB}" >&2
  exit 1
fi

latest_package="$(ls -t "${packages[@]}" | head -n 1)"

if [[ -z "${REMOTE_USER}" || -z "${REMOTE_HOST}" ]]; then
  printf 'Package ready: %s\n' "${latest_package}" >&2
  printf 'Set REMOTE_USER and REMOTE_HOST to deploy, for example:\n' >&2
  printf '  REMOTE_USER=pi REMOTE_HOST=192.168.1.10 ./deploy.sh\n' >&2
  exit 1
fi

remote_target="${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_DIR}"

log "Deploy: scp ${latest_package} ${remote_target}"
scp "${latest_package}" "${remote_target}"

log "Done: ${latest_package} -> ${remote_target}"
