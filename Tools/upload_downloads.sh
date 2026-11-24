#!/usr/bin/env bash
# Upload one build's downloadable packs to the download server; requires rsync and ssh.
#   bash upload_downloads.sh                                        # list builds
#   bash upload_downloads.sh [--dry-run] PLATFORM ID [LINK_ID...]   # upload one
# Sends Saved/Packages/*-downloads/Downloads/PLATFORM/ID/ to <server>/PLATFORM/ID/ (Private/ is
# never read). A file equal to one in server build LINK_ID becomes a hardlink.
# DF_DOWNLOAD_DEST: HOST:DIR or a local DIR. DF_DOWNLOAD_OWNER: USER:GROUP of the uploaded files.
set -euo pipefail

DEST="${DF_DOWNLOAD_DEST:-mf.nofisto.com:/opt/mobileforces/website/public/download}"
OWNER="${DF_DOWNLOAD_OWNER:-mobileforces:mobileforces}"
DIR="${DEST#*:}"
PACKAGES="$(dirname -- "$0")/../Saved/Packages"

on_dest() {
    if [[ "$DEST" == *:* ]]; then
        ssh -- "${DEST%%:*}" "$1"
    else
        bash -c "$1"
    fi
}

if [ $# -eq 0 ]; then
    echo "Local builds:"
    ls -d "$PACKAGES"/*-downloads/Downloads/*/*/ | sed -E 's#.*/([^/]+)/([^/]+)/$#  \1 \2#' || true
    echo "Server builds ($DEST):"
    on_dest "ls -d $DIR/*/[0-9]*-*/" | sed -E 's#.*/([^/]+)/([^/]+)/$#  \1 \2#' || true
    exit 0
fi

# No -t: a new cook gives unchanged paks new times and a hardlink needs equal attributes,
# so -c compares contents. --chown keeps root uploads owned like the server's other builds.
RSYNC=(-rlc --mkpath --chmod=D755,F644 -og "--chown=$OWNER" --partial --info=progress2)
if [ "$1" = --dry-run ]; then
    RSYNC+=(--dry-run --itemize-changes --stats)
    shift
fi
if [ $# -lt 2 ]; then
    sed -n '2,7p' "$0" >&2
    exit 2
fi
PLATFORM="$1"
ID="$2"
shift 2
for LINK_ID in "$@"; do
    RSYNC+=("--link-dest=$DIR/$PLATFORM/$LINK_ID")
done

BUILD=("$PACKAGES"/*-downloads/Downloads/"$PLATFORM"/"$ID")
if [[ "$PLATFORM$ID" == */* || ! -d "${BUILD[0]}" ]]; then
    echo "No build $PLATFORM $ID in $PACKAGES/*-downloads/Downloads/ (run without arguments to list)" >&2
    exit 2
fi
echo "${BUILD[0]}/ -> $DEST/$PLATFORM/$ID/"
rsync "${RSYNC[@]}" -- "${BUILD[0]}/" "$DEST/$PLATFORM/$ID/"
