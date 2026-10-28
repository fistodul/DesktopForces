#!/usr/bin/env bash
# Export individual source commits without switching the working checkout.
#
#   bash Tools/sync_github.sh                 # preview committed changes
#   bash Tools/sync_github.sh --push          # fetch, prepare, then push
#   bash Tools/sync_github.sh --source refs/heads/azure
#
# Requires Bash 4+ and Git. Authors and messages are preserved. Exclusions come
# from the committed .gitignore.github. Conflicts/merges stop before any push.
set -euo pipefail

SOURCE_REF=HEAD
REMOTE=github
BRANCH=main
PUSH=false

while [ $# -gt 0 ]; do
    case "$1" in
        --source|--remote|--branch)
            if [ $# -lt 2 ]; then
                echo "Missing value for $1" >&2
                exit 2
            fi
            case "$1" in
                --source) SOURCE_REF="$2" ;;
                --remote) REMOTE="$2" ;;
                --branch) BRANCH="$2" ;;
            esac
            shift 2
            ;;
        --push)
            PUSH=true
            shift
            ;;
        --dry-run)
            PUSH=false
            shift
            ;;
        -h|--help)
            sed -n '2,9p' "$0"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

cd "$(git rev-parse --show-toplevel)"
REMOTE_REF="refs/remotes/$REMOTE/$BRANCH"
git check-ref-format "$REMOTE_REF"
if "$PUSH"; then
    git fetch -- "$REMOTE" "refs/heads/$BRANCH:$REMOTE_REF"
fi
if git show-ref --verify --quiet "refs/heads/$SOURCE_REF"; then
    SOURCE_REF="refs/heads/$SOURCE_REF"
fi
SOURCE_COMMIT="$(git rev-parse --verify --end-of-options "$SOURCE_REF^{commit}")"
TARGET_COMMIT="$(git rev-parse --verify "$REMOTE_REF")"
BASE="$(git merge-base "$SOURCE_COMMIT" "$TARGET_COMMIT")"

# New exports record source SHAs. Patch IDs recognize the older cherry-picks.
MARKERS="$(git log --format='%(trailers:key=DesktopForces-Source,valueonly)' "$TARGET_COMMIT")"
if [[ "$MARKERS" =~ ([0-9a-f]{64}|[0-9a-f]{40}) ]]; then
    BASE="${BASH_REMATCH[1]}"
    git merge-base --is-ancestor "$BASE" "$SOURCE_COMMIT"
fi
COMMITS="$(git rev-list --reverse "$BASE..$SOURCE_COMMIT")"
if [ -z "$COMMITS" ]; then
    echo "Already up to date."
    echo "Prepared head: $TARGET_COMMIT (0 commits)"
    exit 0
fi
MERGES="$(git rev-list --min-parents=2 "$BASE..$SOURCE_COMMIT")"
if [ -n "$MERGES" ]; then
    echo "Merge commits need manual export; stopping without pushing:" >&2
    echo "$MERGES" >&2
    exit 1
fi

TEMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/df-public.XXXXXXXX")"
trap 'rm -rf -- "$TEMP_DIR"' EXIT
FILTER_INDEX="$TEMP_DIR/filter-index"
APPLY_INDEX="$TEMP_DIR/apply-index"
git show "$SOURCE_COMMIT:.gitignore.github" > "$TEMP_DIR/ignore"
POLICY_BLOB="$(git rev-parse "$SOURCE_COMMIT:.gitignore.github")"

declare -A TREES KNOWN_PATCHES
while read -r PATCH_ID _; do
    KNOWN_PATCHES["$PATCH_ID"]=1
done < <(git log --no-merges --pretty=medium --binary --no-ext-diff --no-textconv -p "$BASE..$TARGET_COMMIT" | git patch-id --stable)

# Only Git indexes/objects are used. No content files are copied or checked out.
# RESULT_TREE returns the tree ID without losing the cache in a subshell.
filtered_tree() {
    local commit="$1"
    local blob="$POLICY_BLOB"

    if [[ -n "${TREES[$commit]:-}" ]]; then
        RESULT_TREE="${TREES[$commit]}"
        return
    fi
    GIT_INDEX_FILE="$FILTER_INDEX" git read-tree "$commit"
    GIT_INDEX_FILE="$FILTER_INDEX" git ls-files -z --cached --ignored \
        --exclude-from="$TEMP_DIR/ignore" > "$TEMP_DIR/ignored"
    GIT_INDEX_FILE="$FILTER_INDEX" git update-index --force-remove -z --stdin < "$TEMP_DIR/ignored"

    # Historical policy makes newly allowed files real additions. Today's policy
    # still filters every commit, so old commits cannot reintroduce excluded assets.
    if git show "$commit:.gitignore.github" > "$TEMP_DIR/historical-ignore" 2>/dev/null; then
        blob="$(git rev-parse "$commit:.gitignore.github")"
    else
        cp "$TEMP_DIR/ignore" "$TEMP_DIR/historical-ignore"
    fi
    GIT_INDEX_FILE="$FILTER_INDEX" git ls-files -z --cached --ignored \
        --exclude-from="$TEMP_DIR/historical-ignore" > "$TEMP_DIR/ignored"
    GIT_INDEX_FILE="$FILTER_INDEX" git update-index --force-remove -z --stdin < "$TEMP_DIR/ignored"
    GIT_INDEX_FILE="$FILTER_INDEX" git update-index --add --cacheinfo 100644 "$blob" .gitignore
    RESULT_TREE="$(GIT_INDEX_FILE="$FILTER_INDEX" git write-tree)"
    TREES["$commit"]="$RESULT_TREE"
}

PUBLIC_HEAD="$TARGET_COMMIT"
COUNT=0
filtered_tree "$TARGET_COMMIT"
if [ "$RESULT_TREE" != "$(git rev-parse "$TARGET_COMMIT^{tree}")" ]; then
    PUBLIC_HEAD="$(git commit-tree "$RESULT_TREE" -p "$PUBLIC_HEAD" -m 'Apply public export exclusions')"
    COUNT=$((COUNT + 1))
    echo "Prepare public ignore policy"
fi
GIT_INDEX_FILE="$APPLY_INDEX" git read-tree "$RESULT_TREE"
echo "Export $SOURCE_COMMIT to $REMOTE_REF. Committed files only."

for COMMIT in $COMMITS; do
    filtered_tree "$(git rev-parse "$COMMIT^")"
    PARENT_TREE="$RESULT_TREE"
    filtered_tree "$COMMIT"
    git diff --binary --full-index --no-renames --no-ext-diff --no-textconv \
        "$PARENT_TREE" "$RESULT_TREE" > "$TEMP_DIR/patch"
    SUBJECT="$(git show -s --format=%s "$COMMIT")"
    if [ ! -s "$TEMP_DIR/patch" ]; then
        echo "Skip content/empty: ${COMMIT:0:10} $SUBJECT"
        continue
    fi
    read -r PATCH_ID _ < <(git patch-id --stable < "$TEMP_DIR/patch")
    if [[ -n "${KNOWN_PATCHES[$PATCH_ID]:-}" ]]; then
        echo "Skip already exported: ${COMMIT:0:10} $SUBJECT"
        continue
    fi
    if ! GIT_INDEX_FILE="$APPLY_INDEX" git apply --cached --3way --whitespace=nowarn "$TEMP_DIR/patch"; then
        echo "Conflict exporting $COMMIT ($SUBJECT); nothing pushed, checkout unchanged." >&2
        exit 1
    fi
    TREE="$(GIT_INDEX_FILE="$APPLY_INDEX" git write-tree)"
    if [ "$TREE" = "$(git rev-parse "$PUBLIC_HEAD^{tree}")" ]; then
        echo "Skip already applied: ${COMMIT:0:10} $SUBJECT"
        continue
    fi
    mapfile -d '' -t META < <(git show -s --format='%an%x00%ae%x00%aI%x00%B%x00' "$COMMIT")
    printf '%s\n\nDesktopForces-Source: %s\n' "${META[3]}" "$COMMIT" > "$TEMP_DIR/message"
    PUBLIC_HEAD="$(GIT_AUTHOR_NAME="${META[0]}" GIT_AUTHOR_EMAIL="${META[1]}" GIT_AUTHOR_DATE="${META[2]}" \
        git commit-tree "$TREE" -p "$PUBLIC_HEAD" -F "$TEMP_DIR/message")"
    KNOWN_PATCHES["$PATCH_ID"]=1
    COUNT=$((COUNT + 1))
    echo "Prepare: ${COMMIT:0:10} $SUBJECT"
done

echo "Prepared head: $PUBLIC_HEAD ($COUNT commits)"
if [ "$COUNT" -eq 0 ]; then
    echo "Already up to date."
elif "$PUSH"; then
    git push -- "$REMOTE" "$PUBLIC_HEAD:refs/heads/$BRANCH"
else
    echo "Preview only; no refs changed or push performed."
    echo "Inspect: git diff $TARGET_COMMIT $PUBLIC_HEAD"
fi
