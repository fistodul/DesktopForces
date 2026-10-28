#!/usr/bin/env bash
# Package Desktop Forces (BuildCookRun): a build that starts on the main menu.
#
#   Tools/package.sh                      Linux, Shipping, with your classic content
#   Tools/package.sh Linux Development    a build with the console and logs
#   Tools/package.sh --public             without the Classic and Unverified plugins
#   Tools/package.sh Android Development --maps MF_TestRange,mf-warehouse
#                                         an arm64 APK (ASTC textures) with two maps
#
# Output: Saved/Packages/<Platform>-<Config>/ (run Linux/DesktopForces.sh, or the .exe on
# Windows; Windows builds: Tools/package.ps1 on a Windows machine). Engine path:
# mfport.toml `ue_root` or $UE_ROOT.
#
# The classic plugin holds content converted from YOUR copy of Mobile Forces: a build with
# it is for you and the team, never for sharing; so is Unverified (community content whose
# authors aren't confirmed yet). --public leaves both out (core maps that still
# borrow classic textures then miss them: `mfport.py packs` lists what needs replacement art).
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPROJECT="$PROJECT_DIR/DesktopForces.uproject"
UE_ROOT="$(python3 "$PROJECT_DIR/Tools/play.py" --engine-root)"

PLATFORM=Linux
CONFIG=Shipping
PUBLIC=0
ONLY_MAPS=""
NEXT_MAPS=0
for A in "$@"; do
  if [ "$NEXT_MAPS" = 1 ]; then
    ONLY_MAPS="$A"
    NEXT_MAPS=0
    continue
  fi
  case "$A" in
    --public) PUBLIC=1 ;;
    --maps) NEXT_MAPS=1 ;;
    Linux|Win64|Android) PLATFORM="$A" ;;
    Shipping|Development) CONFIG="$A" ;;
    *)
      echo "unknown argument: $A" >&2
      exit 2
      ;;
  esac
done
SUFFIX=""
NOTE=""
if [ "$PUBLIC" = 1 ]; then
  SUFFIX="-public"
  NOTE=" (public: no classic content)"
fi
DEBUGINFO=()
if [ "$CONFIG" = Shipping ]; then
  DEBUGINFO+=(-nodebuginfo)
fi
PLATFORM_ARGS=()
if [ "$PLATFORM" = Android ]; then
  # SDK / NDK / JDK from the environment, else where Android Studio or sdkmanager puts them;
  # the versions are the engine's (Engine/Config/Android/Android_SDK.json)
  export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}"
  if [ -z "${NDKROOT:-}" ]; then
    NDKROOT="$(ls -d "$ANDROID_HOME"/ndk/* 2>/dev/null | sort -V | tail -1)"
    export NDKROOT
  fi
  export NDK_ROOT="$NDKROOT"
  if [ -z "${JAVA_HOME:-}" ] && command -v javac >/dev/null; then
    JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")"
    export JAVA_HOME
  fi
  if [ ! -d "$ANDROID_HOME/platforms" ] || [ -z "$NDKROOT" ] || [ -z "${JAVA_HOME:-}" ]; then
    echo "Android needs the SDK, NDK and a JDK: see Docs/Building.md (Android)" >&2
    exit 2
  fi
  PLATFORM_ARGS+=(-cookflavor=ASTC -package) # -package: the APK
fi
OUT="$PROJECT_DIR/Saved/Packages/$PLATFORM-$CONFIG$SUFFIX"

if [ $PUBLIC = 1 ]; then
  # switch the plugin off in the project for this build only, back on whatever happens
  PROJECT_BACKUP="$(mktemp)"
  cp "$UPROJECT" "$PROJECT_BACKUP"
  trap 'cp "$PROJECT_BACKUP" "$UPROJECT"; rm -f "$PROJECT_BACKUP"' EXIT
  python3 - "$UPROJECT" <<'PY'
import json
import sys
from pathlib import Path

project = Path(sys.argv[1])
data = json.loads(project.read_text())
excluded = ('MobileForcesClassic', 'MobileForcesUnverified')
# These plugins are enabled by default when present; explicitly disable them.
data['Plugins'] = [plugin for plugin in data.get('Plugins', []) if plugin['Name'] not in excluded]
data['Plugins'].extend({'Name': name, 'Enabled': False} for name in excluded)
project.write_text(json.dumps(data, indent='\t') + '\n')
PY
fi

# content folders differing only in case (a Linux NTFS / ext4 checkout allows them): UE names
# packages case-insensitively, so one twin hides the other's packages from the cooker, and
# Windows can't hold both. Merge them (the importer follows the texture folder's spelling).
TWINS="$(find "$PROJECT_DIR/Content" "$PROJECT_DIR/Plugins" -type d -path '*Content*' 2>/dev/null | awk '{ l = tolower($0); if (seen[l]++) print }')"
if [ -n "$TWINS" ]; then
  echo "Content folders that differ only in case:" >&2
  echo "$TWINS" >&2
  exit 2
fi

# every built map (the cooker doesn't take maps from the always-cook folders): the game's own
# and, unless --public, the content plugins'
MAPS=""
for D in "$PROJECT_DIR/Content/Maps:/Game/Maps" "$PROJECT_DIR/Content/MF/Maps:/Game/MF/Maps" \
         "$PROJECT_DIR/Plugins/MobileForcesClassic/Content/Maps:/MobileForcesClassic/Maps" \
         "$PROJECT_DIR/Plugins/MobileForcesUnverified/Content/Maps:/MobileForcesUnverified/Maps"; do
  DIR="${D%%:*}"
  ROOT="${D##*:}"
  if [ "$PUBLIC" = 1 ] && [[ "$ROOT" == /MobileForces* ]]; then
    continue
  fi
  for M in "$DIR"/*.umap; do
    NAME="$(basename "$M" .umap)"
    if [ -n "$ONLY_MAPS" ] && [[ ",$ONLY_MAPS," != *",$NAME,"* ]]; then
      continue
    fi
    [ -e "$M" ] && MAPS="$MAPS${MAPS:++}$ROOT/$NAME"
  done
done
# -NoUBA: UE's build accelerator fails renaming files when the checkout is reached through a
# symlink (a shared NTFS checkout); play.py builds the same way
echo "Packaging $PLATFORM $CONFIG$NOTE -> $OUT ($(echo "$MAPS" | tr '+' '\n' | grep -c .) maps)"
"$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
  -project="$UPROJECT" -noP4 -utf8output -unattended \
  -platform="$PLATFORM" -clientconfig="$CONFIG" \
  -ubtargs="-NoUBA" -build -cook -map="$MAPS" -stage -pak -iostore -compressed -prereqs \
  -archive -archivedirectory="$OUT" "${DEBUGINFO[@]}" "${PLATFORM_ARGS[@]}"
echo "Done: $OUT"
