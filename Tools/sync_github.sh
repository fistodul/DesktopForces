#!/usr/bin/env bash
# sync_github.sh — Push source-only commits to the public GitHub repo.
#
# Remotes:
#   azure   https://filipopo@dev.azure.com/filipopo/DF/_git/DF   ← primary (content included)
#   github  git@github.com:fistodul/DesktopForces.git            ← public (source only)
#
# Branches:
#   main    public branch, tracking github/main (source code only, .gitignore excludes content)
#   azure   content branch, tracking azure/main (source + safe community content)
#
# Day-to-day workflow:
#   git push azure azure:main        # push everything (code + content) to Azure DevOps
#   ./Tools/sync_github.sh           # push source-only commits to GitHub
#   ./Tools/sync_github.sh --dry-run # preview what would be pushed without pushing

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

DRY_RUN=false
[[ "${1:-}" == "--dry-run" ]] && DRY_RUN=true

GITHUB_REMOTE="github"
GITHUB_BRANCH="main"
CURRENT_BRANCH="$(git branch --show-current)"

echo "=== sync_github.sh ==="
echo "  Current branch: $CURRENT_BRANCH"
echo "  Target remote:  $GITHUB_REMOTE/$GITHUB_BRANCH"
echo

# Check remote exists
if ! git remote get-url "$GITHUB_REMOTE" &>/dev/null; then
    echo "Error: Remote '$GITHUB_REMOTE' not found."
    echo "Set it up with:  git remote add github git@github.com:fistodul/DesktopForces.git"
    exit 1
fi

# Ensure working directory is clean
if [[ -n "$(git status --porcelain)" ]]; then
    echo "Error: Working tree has uncommitted changes. Commit or stash first."
    exit 1
fi

if [[ "$CURRENT_BRANCH" == "$GITHUB_BRANCH" ]]; then
    # We are already on main
    COMMITS_TO_PUSH="$(git log --oneline "github/$GITHUB_BRANCH..$GITHUB_BRANCH" 2>/dev/null || git log --oneline -5)"
    if [[ -z "$COMMITS_TO_PUSH" ]]; then
        echo "GitHub branch '$GITHUB_BRANCH' is already up to date with remote."
        exit 0
    fi

    echo "Commits to push to $GITHUB_REMOTE/$GITHUB_BRANCH:"
    echo "$COMMITS_TO_PUSH"
    echo

    if $DRY_RUN; then
        echo "[DRY RUN] Would push $GITHUB_BRANCH -> $GITHUB_REMOTE/$GITHUB_BRANCH"
        exit 0
    fi

    echo "Pushing $GITHUB_BRANCH to $GITHUB_REMOTE/$GITHUB_BRANCH..."
    git push "$GITHUB_REMOTE" "$GITHUB_BRANCH:$GITHUB_BRANCH"
    echo "Done!"
else
    # We are on azure (or another feature branch)
    # Find commits on CURRENT_BRANCH that are not on main
    COMMITS=$(git log --reverse --format="%H" "main..$CURRENT_BRANCH")
    
    if [[ -z "$COMMITS" ]]; then
        # Check if main itself has unpushed commits
        UNPUSHED_ON_MAIN=$(git log --oneline "github/$GITHUB_BRANCH..$GITHUB_BRANCH" 2>/dev/null || true)
        if [[ -z "$UNPUSHED_ON_MAIN" ]]; then
            echo "Everything on '$GITHUB_BRANCH' is already up to date with '$GITHUB_REMOTE'."
            exit 0
        fi
        echo "No new commits on '$CURRENT_BRANCH', but '$GITHUB_BRANCH' has unpushed commits:"
        echo "$UNPUSHED_ON_MAIN"
    else
        echo "Inspecting commits on $CURRENT_BRANCH to sync to $GITHUB_BRANCH..."
        CONTENT_REGEX='^(\.gitignore|\.gitignore\.github|Content/Maps/|Content/MF/Community/|Content/MF/Replacements/|Content/MF/Music/)'
        for commit in $COMMITS; do
            SUBJECT=$(git log -1 --format="%s" "$commit")
            # Check if this commit touches non-content files
            TOUCHES_SOURCE=$(git -c core.quotepath=false diff-tree --no-commit-id --name-only -r "$commit" | grep -v -E "$CONTENT_REGEX" || true)

            if [[ -z "$TOUCHES_SOURCE" ]]; then
                echo "  [SKIP CONTENT] $commit $SUBJECT"
            else
                echo "  [SYNC SOURCE]  $commit $SUBJECT"
            fi
        done
    fi
    echo

    if $DRY_RUN; then
        echo "[DRY RUN] In actual run, source commits would be cherry-picked to $GITHUB_BRANCH and pushed to $GITHUB_REMOTE."
        exit 0
    fi

    # Switch to main and cherry-pick source commits
    git checkout "$GITHUB_BRANCH"
    TRAP_RESTORE="git checkout $CURRENT_BRANCH"
    trap '$TRAP_RESTORE' EXIT

    if [[ -n "$COMMITS" ]]; then
        for commit in $COMMITS; do
            TOUCHES_SOURCE=$(git -c core.quotepath=false diff-tree --no-commit-id --name-only -r "$commit" | grep -v -E "$CONTENT_REGEX" || true)
            if [[ -n "$TOUCHES_SOURCE" ]]; then
                SUBJECT=$(git log -1 --format="%s" "$commit")
                echo "Applying $commit ($SUBJECT)..."
                git cherry-pick "$commit"
            fi
        done
    fi

    echo "Pushing $GITHUB_BRANCH to $GITHUB_REMOTE/$GITHUB_BRANCH..."
    git push "$GITHUB_REMOTE" "$GITHUB_BRANCH:$GITHUB_BRANCH"
    echo "Done!"
fi
