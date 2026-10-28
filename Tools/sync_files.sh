#!/usr/bin/env bash
# Copy project files between local directories; requires Bash, rsync and realpath.
#   bash Tools/sync_files.sh SOURCE DESTINATION [--dry-run] [--delete]
# Use the mounted-drive path when running from Linux, WSL or Git Bash.
# Build outputs, Git data and machine-local settings are excluded on both sides.
set -euo pipefail

if [[ "${1:-}" == -h || "${1:-}" == --help ]]; then
    sed -n '2,5p' "$0"
    exit 0
fi

if [ $# -lt 2 ]; then
    echo "Usage: $0 SOURCE DESTINATION [--dry-run] [--delete]" >&2
    exit 2
fi

SOURCE_DIR="$(realpath -- "$1")"
DEST_DIR="$(realpath -m -- "$2")"
shift 2

if [ ! -f "$SOURCE_DIR/DesktopForces.uproject" ]; then
    echo "Source must be a DesktopForces checkout" >&2
    exit 2
fi

if [ "$SOURCE_DIR" = "$DEST_DIR" ]; then
    echo "Same directory; nothing to sync."
    exit 0
fi

if [[ "$DEST_DIR/" == "$SOURCE_DIR/"* || "$SOURCE_DIR/" == "$DEST_DIR/"* ]]; then
    echo "Source and destination must not contain each other" >&2
    exit 2
fi

OPTIONS=(-a --itemize-changes)
for ARG in "$@"; do
    case "$ARG" in
        --dry-run|--delete)
            OPTIONS+=("$ARG")
            ;;
        *)
            echo "Unknown argument: $ARG" >&2
            exit 2
            ;;
    esac
done

# Excluded destination files are protected from --delete too.
rsync "${OPTIONS[@]}" \
    --exclude=Build/ --exclude=Binaries/ --exclude=Intermediate/ \
    --exclude=Saved/ --exclude=DerivedDataCache/ --exclude=.git \
    --exclude=.vs/ --exclude=.vscode/ --exclude=.idea/ \
    --exclude=__pycache__/ --exclude='*.pyc' --exclude=.venv/ \
    --exclude=/Tools/mfport/work/ --exclude=/Tools/mfport/mfport.toml \
    -- "$SOURCE_DIR/" "$DEST_DIR/"
