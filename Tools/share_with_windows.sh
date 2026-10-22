#!/usr/bin/env bash
# =============================================================================
# share_with_windows.sh - one copy of the project for Linux and Windows (dual boot)
#
# Moves this checkout onto the Windows partition and leaves a symlink where it was: Windows
# builds it in place (Tools\build_windows.ps1), Linux keeps working through the link. Nothing is
# synced or stored twice (sync_to_windows.sh copies instead).
#
#   Tools/share_with_windows.sh             what it would do (nothing is changed)
#   Tools/share_with_windows.sh --apply     do it
#   Tools/share_with_windows.sh --apply --to "/media/me/OS/Users/me/DesktopForces"
#   Tools/share_with_windows.sh --undo      bring it back onto the Linux disk
#
# Why this way round: Linux reads and writes NTFS natively (ntfs3); Windows can't use the Linux
# disk without extra drivers or WSL's network share, which Unreal's tools handle badly.
# Both systems can share one checkout: each builds into its own folders (Binaries/Linux vs Win64,
# Intermediate/Build/Linux vs Win64, Saved/Config/Linux vs Windows); content and the derived
# data cache are the same for both. Tested: the game runs on Linux from a copy on ntfs3.
#
# Needs: the partition mounted read-write (ntfs3 or ntfs-3g; Windows' Fast Startup off, else
# Linux gets it read-only), enough free space there, and nothing of the project running. The
# old folder is kept as <name>.moved-<date> until you delete it.
# The target defaults to C:\Users\<you>\DesktopForces (Tools/winpaths.py; WIN_ROOT, WIN_USER,
# WIN_PROJECT override): short (Windows builds trip over 260-character paths), and writable
# from Linux (Documents carries Windows' read-only folder flag).
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT="$(cd "$SCRIPT_DIR/.." && pwd -L)"   # the path as you use it (may be the link)
MODE=dry
TARGET=""
while [ $# -gt 0 ]; do
  case "$1" in
    --apply) MODE=apply ;;
    --undo) MODE=undo ;;
    --to) TARGET="$2"; shift ;;
    -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "unknown option $1"; exit 2 ;;
  esac
  shift
done

running() {
  # the editor / game / a commandlet with this project open
  pgrep -af "UnrealEditor|UnrealBuildTool|AutomationTool" 2>/dev/null | grep -F "DesktopForces.uproject" | grep -v "share_with_windows" || true
}

if [ "$MODE" = undo ]; then
  if [ ! -L "$PROJECT" ]; then echo "$PROJECT isn't a link to the Windows drive: nothing to undo"; exit 1; fi
  SHARED="$(readlink -f "$PROJECT")"
  [ -n "$(running)" ] && { echo "Close the project first:"; running; exit 1; }
  NEED=$(du -sk "$SHARED" | cut -f1); FREE=$(df -Pk "$(dirname "$PROJECT")" | awk 'NR==2 {print $4}')
  [ "$FREE" -gt $((NEED + NEED / 10)) ] || { echo "Not enough space on the Linux disk ($((NEED / 1048576)) GB needed)"; exit 1; }
  echo "Copying $SHARED back to $PROJECT ..."
  rsync -a --info=progress2 "$SHARED/" "$PROJECT.back/"
  rm "$PROJECT"
  mv "$PROJECT.back" "$PROJECT"
  git -C "$PROJECT" config --unset core.fileMode || true
  echo "Done: $PROJECT is a normal folder again. The Windows copy is still at $SHARED (delete it when you're sure)."
  exit 0
fi

if [ -L "$PROJECT" ]; then
  echo "Already shared: $PROJECT -> $(readlink -f "$PROJECT")"
  exit 0
fi
[ -n "$TARGET" ] || TARGET="$(python3 "$SCRIPT_DIR/winpaths.py" project)"
PARENT="$(dirname "$TARGET")"
NEED=$(du -sk "$PROJECT" | cut -f1)
FREE=$(df -Pk "$PARENT" 2>/dev/null | awk 'NR==2 {print $4}')
FSTYPE=$(findmnt -n -o FSTYPE --target "$PARENT" 2>/dev/null || echo "?")
WINPATH=$(python3 "$SCRIPT_DIR/winpaths.py" to-windows "$PARENT" 2>/dev/null || echo "?")

echo "Project:  $PROJECT ($((NEED / 1048576)) GB)"
echo "Target:   $TARGET ($FSTYPE, $(( ${FREE:-0} / 1048576 )) GB free; in Windows: $WINPATH\\$(basename "$TARGET"))"
PROBLEMS=0
case "$FSTYPE" in ntfs3|ntfs|fuseblk) ;; *) echo "  ! the target isn't on an NTFS partition"; PROBLEMS=1 ;; esac
[ -w "$PARENT" ] || { echo "  ! $PARENT isn't writable (mounted read-only? Windows Fast Startup / hibernation)"; PROBLEMS=1; }
[ -e "$TARGET" ] && [ -n "$(ls -A "$TARGET" 2>/dev/null)" ] && { echo "  ! $TARGET already exists and isn't empty"; PROBLEMS=1; }
[ "${FREE:-0}" -gt $((NEED + NEED / 10)) ] || { echo "  ! not enough free space there"; PROBLEMS=1; }
[ -n "$(running)" ] && { echo "  ! close the project first:"; running; PROBLEMS=1; }

if [ "$MODE" = dry ]; then
  echo
  echo "Would: copy everything there, check the copy, set git core.fileMode=false (NTFS has no"
  echo "exec bits), rename this folder to $(basename "$PROJECT").moved-<date> and put a link to"
  echo "the copy in its place. Run with --apply to do it."
  exit $PROBLEMS
fi
[ $PROBLEMS = 0 ] || exit 1

mkdir -p "$TARGET"
echo "Copying (this takes a while) ..."
rsync -a --info=progress2 "$PROJECT/" "$TARGET/"
# a second pass must find nothing left to copy
if [ -n "$(rsync -a --dry-run --itemize-changes "$PROJECT/" "$TARGET/" | head -1)" ]; then
  echo "The copy differs from the original: stopping (nothing was moved; $TARGET can be deleted)"
  exit 1
fi
git -C "$TARGET" config core.fileMode false
BACKUP="$PROJECT.moved-$(date +%Y%m%d-%H%M)"
mv "$PROJECT" "$BACKUP"
ln -s "$TARGET" "$PROJECT"
echo
echo "Done. $PROJECT -> $TARGET"
echo "Windows: open $WINPATH\\$(basename "$TARGET") (Tools\\build_windows.ps1 builds and packages)."
echo "When everything works, delete the old copy:  rm -rf \"$BACKUP\""
