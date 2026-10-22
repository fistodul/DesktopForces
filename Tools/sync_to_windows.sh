#!/usr/bin/env bash
# =============================================================================
# sync_to_windows.sh - Sync DesktopForces source files to the Windows partition
#
# Copies/syncs only non-intermediate, platform-agnostic project files:
#   Config/, Content/, Source/, Tools/, Plugins/, and DesktopForces.uproject
# Excludes: Build/ (Windows' packaged games: --delete must not touch them), Binaries/,
#           Intermediate/, Saved/, DerivedDataCache/, .git/
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DRY=""
if [ "${1:-}" = "--dry-run" ]; then DRY="--dry-run"; shift; fi   # list what would be copied
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Destination: the first argument, else $WIN_PROJECT, else C:\Users\<you>\DesktopForces as
# Tools/winpaths.py finds it (WIN_ROOT / WIN_USER override the guesses). --dry-run lists what
# would be copied. (Tools/share_with_windows.sh keeps a single copy for both systems instead.)
WIN_DEST="${1:-$(python3 "$SCRIPT_DIR/winpaths.py" project)}"

echo "=========================================================="
echo " Syncing DesktopForces to Windows partition"
echo " Source: $PROJECT_DIR"
echo " Destination: $WIN_DEST"
echo "=========================================================="

if [ ! -d "$(dirname "$(dirname "$WIN_DEST")")" ]; then
    echo "ERROR: $(dirname "$(dirname "$WIN_DEST")") isn't there. Is the Windows drive mounted (read-write)?"
    exit 1
fi

[ -n "$DRY" ] || mkdir -p "$WIN_DEST"

# Rsync non-intermediate folders and project file
rsync -av $DRY --delete \
    --exclude='/Build/' \
    --exclude='Binaries/' \
    --exclude='Intermediate/' \
    --exclude='Saved/' \
    --exclude='DerivedDataCache/' \
    --exclude='.git/' \
    --exclude='.github/' \
    --exclude='.vscode/' \
    --exclude='__pycache__/' \
    --exclude='Tools/mfport/work/' \
    --exclude='*.pyc' \
    --exclude='Plugins/*/Binaries/' \
    --exclude='Plugins/*/Intermediate/' \
    "$PROJECT_DIR/" "$WIN_DEST/"

echo "=========================================================="
if [ -n "$DRY" ]; then echo " Dry run: nothing was copied"; else echo " Sync complete!"; fi
echo " In Windows, open: $(python3 "$SCRIPT_DIR/winpaths.py" to-windows "$WIN_DEST" 2>/dev/null || echo "$WIN_DEST")"
echo " Run: powershell -ExecutionPolicy Bypass -File Tools\\build_windows.ps1"
echo "=========================================================="
