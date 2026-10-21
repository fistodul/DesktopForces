#!/usr/bin/env bash
# Package Desktop Forces (BuildCookRun): a build that starts on the main menu.
#
#   Tools/package.sh                      Linux, Shipping, with your classic content
#   Tools/package.sh Linux Development    a build with the console and logs
#   Tools/package.sh --public             without the Classic and Unverified plugins
#
# Output: Saved/Packages/<Platform>-<Config>/ (run Linux/DesktopForces.sh, or the .exe on
# Windows; Windows builds: Tools/build_windows.ps1 on a Windows machine). Engine path:
# mfport.toml `ue_root` or $UE_ROOT.
#
# The classic plugin holds content converted from YOUR copy of Mobile Forces: a build with
# it is for you and the team, never for sharing; so is Unverified (community content whose
# authors aren't confirmed yet). --public leaves both out (core maps that still
# borrow classic textures then miss them: `mfport.py packs` lists what needs replacement art).
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPROJECT="$PROJECT_DIR/DesktopForces.uproject"
UE_ROOT="${UE_ROOT:-$(python3 -c "import sys; sys.path.insert(0, '$PROJECT_DIR/Tools/mfport'); from mflib import config; print(config.load().ue_root)")}"

PLATFORM=Linux
CONFIG=Shipping
PUBLIC=0
for A in "$@"; do
  case "$A" in
    --public) PUBLIC=1 ;;
    Linux|Win64) PLATFORM="$A" ;;
    Shipping|Development) CONFIG="$A" ;;
    *) echo "unknown argument: $A" >&2; exit 2 ;;
  esac
done
SUFFIX=""; NOTE=""
if [ $PUBLIC = 1 ]; then SUFFIX="-public"; NOTE=" (public: no classic content)"; fi
DEBUGINFO=""
if [ "$CONFIG" = Shipping ]; then DEBUGINFO="-nodebuginfo"; fi
OUT="$PROJECT_DIR/Saved/Packages/$PLATFORM-$CONFIG$SUFFIX"

if [ $PUBLIC = 1 ]; then
  # switch the plugin off in the project for this build only, back on whatever happens
  cp "$UPROJECT" "$UPROJECT.bak"
  trap 'mv "$UPROJECT.bak" "$UPROJECT"' EXIT
  python3 - "$UPROJECT" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
# the content plugins aren't listed (EnabledByDefault: on when present): list them as off
d['Plugins'] = [p for p in d.get('Plugins', []) if p['Name'] not in ('MobileForcesClassic', 'MobileForcesUnverified')]
d['Plugins'] += [{'Name': 'MobileForcesClassic', 'Enabled': False}, {'Name': 'MobileForcesUnverified', 'Enabled': False}]
json.dump(d, open(p, 'w'), indent='\t')
PY
fi

echo "Packaging $PLATFORM $CONFIG$NOTE -> $OUT"
"$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
  -project="$UPROJECT" -noP4 -utf8output -unattended \
  -platform="$PLATFORM" -clientconfig="$CONFIG" \
  -build -cook -stage -pak -iostore -compressed -prereqs \
  -archive -archivedirectory="$OUT" $DEBUGINFO
echo "Done: $OUT"
