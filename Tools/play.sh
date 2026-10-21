#!/usr/bin/env bash
# Start Desktop Forces from the editor build (no packaging needed).
#
#   Tools/play.sh                              warehouse, deathmatch
#   Tools/play.sh mf-warehouse tdm 8           team deathmatch with 8 bots
#   Tools/play.sh mf-warehouse koh 8           King of the Hill (MF-mods)
#   Tools/play.sh MF_TestRange                 test map
#   Tools/play.sh editor                       open the editor instead
#   Tools/play.sh build                        compile the C++ modules first
#
# Modes: dm, tdm, holdout, sc (Safecracker), ctf, det (Detonation), cap (Captains), trailer, koh, tug, coop, zombie, arena, moball, br.
# No arguments at all would start the main menu: run the editor binary with -game and no map. Extra arguments after the bot count go to the engine
# (e.g. -windowed -ResX=1600 -ResY=900, -log; without them the saved video settings apply). Engine path: mfport.toml `ue_root` or $UE_ROOT.
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPROJECT="$PROJECT_DIR/DesktopForces.uproject"     # UE needs an absolute path here
UE_ROOT="${UE_ROOT:-$(python3 -c "import sys; sys.path.insert(0, '$PROJECT_DIR/Tools/mfport'); from mflib import config; print(config.load().ue_root)")}"
EDITOR="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor"

case "${1:-}" in
  build)
    exec "$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" DesktopForcesEditor Linux Development \
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
  *)    # cleared community maps are part of the game (/Game/Maps), the rest is in the classic plugin
        if [ -f "$PROJECT_DIR/Content/Maps/$MAP.umap" ]; then MAP_PATH="/Game/Maps/$MAP"
        else MAP_PATH="/MobileForcesClassic/Maps/$MAP"; fi ;;
esac
case "$MODE" in
  dm)      GAME="/Script/DesktopForces.MFGameMode_DM" ;;
  tdm)     GAME="/Script/DesktopForces.MFGameMode_TDM" ;;
  holdout) GAME="/Script/DesktopForces.MFGameMode_Holdout" ;;
  sc)      GAME="/Script/DesktopForces.MFGameMode_Safecracker" ;;
  ctf)     GAME="/Script/DesktopForces.MFGameMode_CTF" ;;
  det)     GAME="/Script/DesktopForces.MFGameMode_Detonation" ;;
  cap)     GAME="/Script/DesktopForces.MFGameMode_Captains" ;;
  koh)     GAME="/Script/DesktopForces.MFGameMode_KOH" ;;
  tug)     GAME="/Script/DesktopForces.MFGameMode_Tug" ;;
  trailer) GAME="/Script/DesktopForces.MFGameMode_Trailer" ;;
  coop)    GAME="/Script/DesktopForces.MFGameMode_Coop" ;;
  zombie)  GAME="/Script/DesktopForces.MFGameMode_Zombie" ;;
  arena)   GAME="/Script/DesktopForces.MFGameMode_Arena" ;;
  moball)  GAME="/Script/DesktopForces.MFGameMode_MoBall" ;;
  br)      GAME="/Script/DesktopForces.MFGameMode_BattleRoyale" ;;
  *)       GAME="$MODE" ;;
esac

# MF_OPTS: extra URL options, e.g. MF_OPTS="?TimeLimit=5?Part=2"
exec "$EDITOR" "$UPROJECT" "$MAP_PATH?game=$GAME?Bots=$BOTS${MF_OPTS:-}" -game "$@"
