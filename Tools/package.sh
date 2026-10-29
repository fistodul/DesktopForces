#!/usr/bin/env bash
# Package Desktop Forces (BuildCookRun): a build that starts on the main menu.
#
#   Tools/package.sh                      Linux, Shipping, with your classic content
#   Tools/package.sh Linux Development    a build with the console and logs
#   Tools/package.sh --public             without the Classic and Unverified plugins
#   Tools/package.sh Android Development --maps MF_TestRange,mf-warehouse
#                                         an arm64 APK (ASTC textures) with two maps
#   Tools/package.sh Android --downloads  the APK / OBB with the core pack only; the community and
#                                         unverified packs go to <output>-downloads/Downloads/ for
#                                         the server, classic to .../Sideload/ (never upload it)
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
DOWNLOADS=0
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
    --downloads) DOWNLOADS=1 ;;
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
PACKAGE_ARGS=()
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
  PLATFORM_ARGS+=(-cookflavor=ASTC)
  PACKAGE_ARGS=(-package) # the APK
fi
OUT="$PROJECT_DIR/Saved/Packages/$PLATFORM-$CONFIG$SUFFIX"

# the content build id (UDFDownloads): only in a --downloads build, only while it's made - the
# paks on the server must come from the same cook as the game asking for them
BUILD_ID_FILE="$PROJECT_DIR/Content/Downloads/ContentBuildId.txt"
mkdir -p "$(dirname "$BUILD_ID_FILE")" # staged as UFS (DefaultGame.ini), empty unless --downloads
rm -f "$BUILD_ID_FILE"
PROJECT_BACKUP=""
cleanup() {
  rm -f "$BUILD_ID_FILE"
  if [ -n "$PROJECT_BACKUP" ]; then
    cp "$PROJECT_BACKUP" "$UPROJECT"
    rm -f "$PROJECT_BACKUP"
  fi
}
trap cleanup EXIT

if [ $PUBLIC = 1 ]; then
  # switch the plugin off in the project for this build only, back on whatever happens
  PROJECT_BACKUP="$(mktemp)"
  cp "$UPROJECT" "$PROJECT_BACKUP"
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

# a fresh output folder: paks left from an earlier build (the single pak before chunking, a
# pack since dropped) would be mounted too
rm -rf "$OUT"

# content packs -> their own paks (bGenerateChunks): label each pack root (cheap once they exist)
mkdir -p "$PROJECT_DIR/Saved/Logs"
if ! "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$UPROJECT" -run=DFImport -steps=packs \
    -unattended -nopause -nosplash > "$PROJECT_DIR/Saved/Logs/PackLabels.log" 2>&1; then
  echo "Labelling the content packs failed: Saved/Logs/PackLabels.log" >&2
  exit 2
fi
if grep -q "duplicate PrimaryAssetID" "$PROJECT_DIR/Saved/Logs/PackLabels.log"; then
  # only one of two same-named labels counts: the other's folder would fall into the core pak
  grep "duplicate PrimaryAssetID" "$PROJECT_DIR/Saved/Logs/PackLabels.log" | sort -u >&2
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
UAT=("$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun
  -project="$UPROJECT" -noP4 -utf8output -unattended
  -platform="$PLATFORM" -clientconfig="$CONFIG" -pak -iostore -compressed
  "${DEBUGINFO[@]}" "${PLATFORM_ARGS[@]}")
if [ "$DOWNLOADS" = 0 ]; then
  "${UAT[@]}" -ubtargs="-NoUBA" -build -cook -map="$MAPS" -stage -prereqs \
    -archive -archivedirectory="$OUT" "${PACKAGE_ARGS[@]}"
  echo "Done: $OUT"
  exit 0
fi

# --downloads: cook and stage, take the packs other than core (chunk 0) out of the staged build
# (Tools/downloads.py: community / unverified to Downloads/, classic to Sideload/), then package /
# archive what's left
case "$PLATFORM" in
  Android) STAGE_NAME=Android_ASTC MANIFEST_PLATFORM=Android ;;
  Win64) STAGE_NAME=Windows MANIFEST_PLATFORM=Windows ;;
  *) STAGE_NAME="$PLATFORM" MANIFEST_PLATFORM="$PLATFORM" ;;
esac
BUILD_ID="$(date -u +%Y%m%d-%H%M%S)-$(git -C "$PROJECT_DIR" rev-parse --short HEAD 2>/dev/null || echo local)"
echo "$BUILD_ID" > "$BUILD_ID_FILE"
STAGE="$PROJECT_DIR/Saved/StagedBuilds/$STAGE_NAME"
rm -f "$STAGE".obb "$STAGE".*.obb # else the OBB of an earlier build, with every pack, is reused
"${UAT[@]}" -ubtargs="-NoUBA" -build -cook -map="$MAPS" -stage -prereqs
python3 "$PROJECT_DIR/Tools/downloads.py" "$STAGE/DesktopForces/Content/Paks" \
  "$PROJECT_DIR/Saved/Cooked/$STAGE_NAME/DesktopForces/Metadata/ChunkManifest" "$OUT-downloads" "$BUILD_ID" "$MANIFEST_PLATFORM"
"${UAT[@]}" -skipcook -skipstage -skippak -archive -archivedirectory="$OUT" "${PACKAGE_ARGS[@]}"
echo "Done: $OUT (content build $BUILD_ID)"
echo "  upload the contents of $OUT-downloads/Downloads/ to the download server (DownloadServer in DefaultGame.ini)"
echo "  $OUT-downloads/Sideload/: the classic pack - for your own devices only (Docs/Building.md)"
