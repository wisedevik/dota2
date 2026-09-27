#!/usr/bin/env bash
# Starts the client against a local gcserver. Build first, it installs
# libsteam_api.dylib and window_fix.dylib into the client.
#
#   devtools/run_client.sh [game arguments...]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME="$ROOT/client/game/game"
BIN="$GAME/bin/osx64"

if [ ! -f "$BIN/window_fix.dylib" ]; then
  echo "window_fix.dylib is not installed; run: cmake --build build" >&2
  exit 1
fi

cd "$GAME"
exec env \
  DOTA_CM_PUBKEY="${DOTA_CM_PUBKEY:-$ROOT/build/cm_rsa.pub.der}" \
  DYLD_LIBRARY_PATH="$BIN" \
  DYLD_INSERT_LIBRARIES="$BIN/window_fix.dylib" \
  ./bin/osx64/dota2.app/Contents/MacOS/dota2 \
  -steam +@panorama_min_comp_layer_dimension 0 -prewarm_panorama "$@"
