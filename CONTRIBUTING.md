# Contributing to Desktop Forces

Thanks for helping. Fixes, features, maps, art and docs are all welcome.

## Sign off your commits (DCO)

Every commit in a pull request must carry a `Signed-off-by` line with your name and email:

    git commit -s -m "Fix the shotgun reload"

    Signed-off-by: Your Name <you@example.com>

With the sign-off you certify the [Developer Certificate of Origin](DCO). In short, you wrote the
change, or have the right to submit it, under the project's licence. A check on every pull request
looks for the line. To sign off commits you already made: `git rebase --signoff <base>`, then
force-push your branch.

## Licence

Code, tools and docs are under the [Mozilla Public License 2.0](LICENSE.md). Your contribution is
licensed the same way (inbound = outbound), and you keep the copyright on what you wrote.

Only contribute what you have the rights to:
- **No original Mobile Forces data** (maps, textures, sounds, models). The pipeline converts those
  from each player's own copy (`Tools/mfport`).
- **Art and audio:** your own work or freely licensed work, with the licence and author added to
  [CREDITS.md](CREDITS.md).
- **Code from other projects:** only under a licence compatible with the MPL, noted in the file.

## How

- Read [AGENTS.md](AGENTS.md) and the doc for the area you touch in `Docs/`.
  [Building.md](Docs/Building.md) covers setup, the build and tests.
- **Code style:** `.clang-format` (clang-format 18). Run `clang-format -i` on the files you
  changed, and avoid local names that shadow inherited members (`Owner`, `Mesh`): MSVC rejects
  them.
- **Checks:** build the editor target, run the affected gameplay tests, and describe what you
  checked in the pull request.
- **Scope:** one change per pull request, with a message that says what and why.
