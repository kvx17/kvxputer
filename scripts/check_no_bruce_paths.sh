#!/usr/bin/env bash
# Fail CI if legacy Bruce filesystem path literals appear in firmware source.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PATTERN='"/Bruce|"/bruce\.conf|"/brucePins'

if rg -n "$PATTERN" "$ROOT/src" --glob '*.{cpp,h,hpp,c}' \
  --glob '!**/paths.h' \
  --glob '!**/path_migration.cpp'; then
  echo "ERROR: legacy Bruce path literals found in src/ (use kvx::paths::*)."
  exit 1
fi

echo "OK: no legacy Bruce path literals in src/"
