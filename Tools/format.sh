#!/usr/bin/env bash
# Format the C++ with clang-format (style: .clang-format at the project root).
#
#   Tools/format.sh             every .h / .cpp under Source/
#   Tools/format.sh FILE...     just those
#   Tools/format.sh --check     list what isn't formatted (exit 1 if anything)
#
# CLANG_FORMAT picks the binary (default clang-format; written for version 18). Visual Studio,
# Rider and VS Code apply .clang-format by themselves.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
CF="${CLANG_FORMAT:-clang-format}"
CHECK=0
if [ "${1:-}" = "--check" ]; then CHECK=1; shift; fi
if [ $# -gt 0 ]; then FILES=("$@"); else mapfile -t FILES < <(find Source -name '*.h' -o -name '*.cpp' | sort); fi
if [ $CHECK = 1 ]; then
  BAD=0
  for F in "${FILES[@]}"; do
    "$CF" --style=file --dry-run --Werror "$F" > /dev/null 2>&1 || { echo "$F"; BAD=1; }
  done
  exit $BAD
fi
"$CF" --style=file -i "${FILES[@]}"
echo "formatted ${#FILES[@]} files"
