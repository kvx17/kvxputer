#!/usr/bin/env bash
# Run the desktop emulator with a KVX_UI_SCRIPT.
# Usage: scripts/ui_sweep.sh [script] [audit_dir]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="${1:-$ROOT/scripts/ui_audit/smoke.uiscript}"
AUDIT_DIR="${2:-$ROOT/screenshot/audit}"
BIN="$ROOT/build/linux-x86-64/Debug/kvxputerzero"

if [[ ! -x "$BIN" ]]; then
  echo "building linux-x86-64-dbg..."
  cmake --preset linux-x86-64 >/dev/null
  cmake --build --preset linux-x86-64-dbg
fi

mkdir -p "$AUDIT_DIR"
export DISPLAY="${DISPLAY:-:0}"
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
export KVX_UI_SCRIPT="$SCRIPT"
export KVX_AUDIT_DIR="$AUDIT_DIR"
export KVX_SKIP_BOOT=1

echo "running: $BIN"
echo "  script: $SCRIPT"
echo "  audit:  $AUDIT_DIR"
cd "$ROOT"
"$BIN"
echo "done. shots in $AUDIT_DIR:"
ls -1 "$AUDIT_DIR" | tail -n 40
