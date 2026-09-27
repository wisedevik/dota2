#!/usr/bin/env bash
# Downloads the Dota 2 osx64 client, build 1805 (Dec 2016), into client/game.
#
#   STEAM_USER=<login> devtools/fetch_client.sh
#
# Uses DepotDownloader (https://github.com/SteamRE/DepotDownloader), expected at
# devtools/depotdownloader/DepotDownloader. Login is by QR code: scan it with
# the Steam mobile app. The account needs Dota 2 in its library (it is free).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPOT_DOWNLOADER="${DEPOT_DOWNLOADER:-$ROOT/devtools/depotdownloader/DepotDownloader}"
OUT="$ROOT/client/game"
: "${STEAM_USER:?set STEAM_USER to your Steam login}"

# depot:manifest
DEPOTS=(
  373304:6667563824491864000  # macOS binaries
  373301:6583592615927321343  # base content, pak01_dir.vpk
  381451:1164593604345069641  # pak01_000..019
  381452:6608625692772888057  # pak01_020..039
  381453:4543056467534577605  # pak01_040..059
  381454:8363743735196262460  # pak01_060..079
  381455:9205520134704853582  # pak01_080..128
)

[ -x "$DEPOT_DOWNLOADER" ] || { echo "DepotDownloader not found at $DEPOT_DOWNLOADER" >&2; exit 1; }
mkdir -p "$OUT"

for entry in "${DEPOTS[@]}"; do
  depot="${entry%%:*}"
  manifest="${entry#*:}"
  echo "== depot $depot, manifest $manifest"
  "$DEPOT_DOWNLOADER" -app 570 -depot "$depot" -manifest "$manifest" \
    -username "$STEAM_USER" -qr -remember-password -dir "$OUT"
done
