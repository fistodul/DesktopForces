#!/usr/bin/env bash
# Start Mobile Forces from the editor build (no packaging needed).
#
#   Tools/play.sh                              warehouse, deathmatch
#   Tools/play.sh mf-warehouse tdm 8           team deathmatch with 8 bots
#   Tools/play.sh mf-warehouse koh 8           King of the Hill (mod plugin)
#   Tools/play.sh MF_TestRange                 test map
#   Tools/play.sh editor                       open the editor instead
#   Tools/play.sh build                        compile the C++ modules first
#
# Modes: dm, tdm, holdout, sc (Safecracker), ctf, koh. Extra arguments after the bot count go to the engine
# (e.g. -ResX=1600 -ResY=900, -log). Engine path: mfport.toml `ue_root` or $UE_ROOT.
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPROJECT="$PROJECT_DIR/MobileForces.uproject"     # UE needs an absolute path here
UE_ROOT="${UE_ROOT:-$(python3 -c "import sys; sys.path.insert(0, '$PROJECT_DIR/Tools/mfport'); from mflib import config; print(config.load().ue_root)")}"
EDITOR="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor"

case "${1:-}" in
  build)
    exec "$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" MobileForcesEditor Linux Development \
      -project="$UPROJECT" -waitmutex -NoUBA ;;
  editor)
    exec "$EDITOR" "$UPROJECT" ;;
esac

MAP="${1:-mf-warehouse}"
MODE="${2:-dm}"
BOTS="${3:-0}"
shift $(( $# < 3 ? $# : 3 ))

case "$MAP" in
  MF_*) MAP_PATH="/Game/MF/Maps/$MAP" ;;
  /*)   MAP_PATH="$MAP" ;;
  *)    MAP_PATH="/Game/MFMaps/Generated/$MAP" ;;
esac
case "$MODE" in
  dm)      GAME="/Script/MobileForces.MFGameMode_DM" ;;
  tdm)     GAME="/Script/MobileForces.MFGameMode_TDM" ;;
  holdout) GAME="/Script/MobileForces.MFGameMode_Holdout" ;;
  sc)      GAME="/Script/MobileForces.MFGameMode_Safecracker" ;;
  ctf)     GAME="/Script/MobileForces.MFGameMode_CTF" ;;
  koh)     GAME="/Script/MFModKOH.MFGameMode_KOH" ;;
  *)       GAME="$MODE" ;;
esac

# MF_OPTS: extra URL options, e.g. MF_OPTS="?TimeLimit=5?Part=2"
exec "$EDITOR" "$UPROJECT" "$MAP_PATH?game=$GAME?Bots=$BOTS${MF_OPTS:-}" -game -windowed -ResX=1280 -ResY=720 "$@"
