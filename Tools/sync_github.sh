#!/usr/bin/env bash
# sync_github.sh — Push source-only commits to the public GitHub repo.
#
# Remotes:
#   azure   https://filipopo@dev.azure.com/filipopo/DF/_git/DF   ← primary (content included)
#   github  git@github.com:fistodul/DesktopForces.git            ← public (source only)
#
# Day-to-day workflow:
#   git push azure main              # push everything (code + content) to Azure DevOps
#   ./Tools/sync_github.sh           # strip content, push source-only to GitHub
#   ./Tools/sync_github.sh --dry-run # preview without pushing
#
# How it works:
#   1. Creates/updates a local `github` branch by merging `main`
#   2. Swaps .gitignore for the public version (.gitignore.github)
#   3. Removes content files from git's index (NOT from disk)
#   4. Commits and pushes to the `github` remote as `main`

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

DRY_RUN=false
[[ "${1:-}" == "--dry-run" ]] && DRY_RUN=true

GITHUB_REMOTE="github"         # the GitHub remote (renamed from origin)
GITHUB_BRANCH="github"         # local branch used for the stripped push
GITHUB_REMOTE_BRANCH="main"    # branch name on GitHub
CURRENT_BRANCH="$(git branch --show-current)"

# Content paths that are tracked on main but excluded from GitHub
CONTENT_PATHS=(
    "Content/Maps/"
    "Content/MF/Community/"
    "Content/MF/Replacements/"
    "Content/MF/Music/"
)

echo "=== sync_github.sh ==="
echo "  Source branch:  $CURRENT_BRANCH"
echo "  GitHub remote:  $GITHUB_REMOTE ($GITHUB_REMOTE_BRANCH)"
echo

# Check the remote exists
if ! git remote get-url "$GITHUB_REMOTE" &>/dev/null; then
    echo "Error: Remote '$GITHUB_REMOTE' not found."
    echo "Set it up with:  git remote rename origin github"
    echo "             or:  git remote add github git@github.com:fistodul/DesktopForces.git"
    exit 1
fi

# Sanity checks
if [[ -n "$(git status --porcelain)" ]]; then
    echo "Error: Working tree has uncommitted changes. Commit or stash first."
    exit 1
fi

# Create or update the github branch
if git show-ref --verify --quiet "refs/heads/$GITHUB_BRANCH"; then
    echo "Updating $GITHUB_BRANCH branch from $CURRENT_BRANCH..."
    git checkout "$GITHUB_BRANCH"
    # Merge main, but don't commit yet so we can fix .gitignore and strip content
    git merge "$CURRENT_BRANCH" --no-commit --no-ff -X theirs 2>/dev/null || true
else
    echo "Creating $GITHUB_BRANCH branch from $CURRENT_BRANCH..."
    git checkout -b "$GITHUB_BRANCH" "$CURRENT_BRANCH"
    # Reset to allow modification before committing
    git reset HEAD~0
fi

# Restore the public .gitignore (which excludes content)
if [[ -f .gitignore.github ]]; then
    cp .gitignore.github .gitignore
    git add .gitignore
fi

# Remove content files from the index (keeps them on disk)
for path in "${CONTENT_PATHS[@]}"; do
    if git ls-files "$path" | grep -q .; then
        git rm -r --cached --quiet "$path" 2>/dev/null || true
    fi
done

# Also remove .gitignore.github from the public branch (it's internal tooling)
git rm --cached --quiet .gitignore.github 2>/dev/null || true

# Check if there's anything to commit
if git diff --cached --quiet 2>/dev/null; then
    echo "Nothing new to sync — GitHub branch is already up to date."
    git checkout "$CURRENT_BRANCH"
    exit 0
fi

# Commit
MAIN_HEAD="$(git log -1 --format='%h %s' "$CURRENT_BRANCH")"
git commit -m "Sync from main: $MAIN_HEAD" --allow-empty

if $DRY_RUN; then
    echo
    echo "[DRY RUN] Would push $GITHUB_BRANCH → $GITHUB_REMOTE/$GITHUB_REMOTE_BRANCH"
    echo "  Latest commits on $GITHUB_BRANCH:"
    git log --oneline -5
    git checkout "$CURRENT_BRANCH"
    echo
    echo "Run without --dry-run to actually push."
else
    echo
    echo "Pushing to GitHub..."
    git push "$GITHUB_REMOTE" "$GITHUB_BRANCH:$GITHUB_REMOTE_BRANCH"
    echo "Done."
fi

git checkout "$CURRENT_BRANCH"

