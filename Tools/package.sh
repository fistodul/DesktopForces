#!/usr/bin/env bash
# Package Desktop Forces (BuildCookRun): a build that starts on the main menu.
#
#   Tools/package.sh                      Linux, Shipping, with your classic content
#   Tools/package.sh Linux Development    a build with the console and logs
#   Tools/package.sh --public             without the Classic and Unverified plugins
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
for A in "$@"; do
  case "$A" in
    --public) PUBLIC=1 ;;
    Linux|Win64) PLATFORM="$A" ;;
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
    [ -e "$M" ] && MAPS="$MAPS${MAPS:++}$ROOT/$(basename "$M" .umap)"
  done
done
echo "Packaging $PLATFORM $CONFIG$NOTE -> $OUT ($(echo "$MAPS" | tr '+' '\n' | grep -c .) maps)"
"$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
  -project="$UPROJECT" -noP4 -utf8output -unattended \
  -platform="$PLATFORM" -clientconfig="$CONFIG" \
  -build -cook -map="$MAPS" -stage -pak -iostore -compressed -prereqs \
  -archive -archivedirectory="$OUT" "${DEBUGINFO[@]}"
echo "Done: $OUT"
