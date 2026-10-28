# Building and tools

Use Python 3.11+ and Unreal Engine 5.8. The asset pipeline also needs Pillow, numpy,
ffmpeg, a Mobile Forces installation, and Wine on Linux. Windows C++ builds need
Visual Studio's C++ workload, a compatible Windows SDK and .NET Framework SDK;
`package.ps1` checks these before packaging.

## Build and play

`play.py` works on Linux and Windows. Replace `python3` with `py -3` on Windows if
needed. Run `python3 Tools/play.py --help` for the complete examples and mode list.

```bash
python3 Tools/setup.py --check
python3 Tools/play.py build -MaxParallelActions=4
python3 Tools/play.py mf-warehouse tdm 8
python3 Tools/play.py MF_TestRange dm 0
python3 Tools/play.py editor
python3 Tools/play.py --dry-run mf-warehouse dm 0 -RenderOffscreen -nosound
```

The launcher builds the Development editor target, including the game module. It
runs the game through UnrealEditor with `-game`; packaging produces a standalone
executable. Extra arguments are passed to Unreal. `DF_OPTS` adds URL options such
as `?ScoreLimit=10?Parachutes=0`.

Copy `Tools/mfport/mfport.example.toml` to the ignored `mfport.toml` and set your game
and engine paths. Then convert a map with `python3 Tools/mfport/mfport.py all mf-warehouse`.
See [Pipeline.md](Pipeline.md) for individual import steps.

## Engine and platform paths

Engine precedence: `UE_ROOT`, then `MFPORT_UE_ROOT`, then the current platform's
section in `mfport.toml`, then shared keys. Other keys accept `MFPORT_<KEY>`.
Relative paths are relative to `Tools/mfport`; moving a checkout can change what
its sibling-relative paths mean. Use absolute paths or `~` in platform sections:

```toml
work_dir = "work"
uproject = "../../DesktopForces.uproject"
[linux]
ue_root = '~/Linux_Unreal_Engine_5.8.4'
game_dir = '~/Games/MobileForces'

[windows]
game_dir = 'C:\Program Files (x86)\Steam\steamapps\common\Mobile Forces'
ue_root = 'C:\Program Files\Epic Games\UE_5.8'
```

Windows paths need single-quoted TOML strings or forward slashes. Setup escapes
its generated paths automatically. `ue_root = "auto"` selects the newest installed
patch matching the project's major/minor using `Engine/Build/Build.version`.
Explicit paths stay pinned. Linux searches home, the project parent and `/opt`;
Windows checks Epic's registry/installation list and usual install directories.
`python3 Tools/play.py --engine-root` prints the result.

5.8.3 and 5.8.4 use `EngineAssociation = "5.8"`; rebuild C++ modules after switching
patches. There is no need to change the project association to a patch number.

## Package

The scripts have the same basename and platform-appropriate extensions:

```bash
bash Tools/package.sh Linux Development
bash Tools/package.sh --public
```

```powershell
powershell -ExecutionPolicy Bypass -File Tools/package.ps1 -Config Development -Zip
powershell -ExecutionPolicy Bypass -File Tools/package.ps1 -Public
powershell -ExecutionPolicy Bypass -File Tools/package.ps1 -Package:$false
pwsh Tools/package.ps1 -DryRun
```

Both run Unreal's BuildCookRun and include the map list, compressed paks and iostore.
The Windows script also checks the Windows toolchain, builds editor modules and
can produce a ZIP. It uses the launcher's engine discovery unless `-UePath` is given.
Outputs: `Saved/Packages/<platform>-<config>/` on Linux; `Build/Windows/` on Windows.

`--public` / `-Public` disables the Classic and Unverified content plugins for that
build, restoring the project file afterward. Maps still borrowing their textures
will need replacements (`mfport.py packs`). Builds containing original game content
are for the team; do not publish that content with the public source.

## Shared checkout or separate copies

For dual boot, put the checkout on a drive both systems can access and optionally
make a Linux symlink to it with `ln -s`. Moving folders and creating the link are
ordinary filesystem operations; no project-specific migration script is needed.
Both hosts use separate platform build directories. Keep Windows paths short.

To maintain a separate copy, use explicit local paths:

```bash
bash Tools/sync_files.sh /path/to/DesktopForces /path/to/copy --dry-run
bash Tools/sync_files.sh /path/to/DesktopForces /path/to/copy
bash Tools/sync_files.sh /path/to/DesktopForces /path/to/copy --delete
```

Requires Bash, rsync and realpath (Linux/WSL/Git Bash with these installed). Build
outputs, Git data, caches and local `mfport.toml` are excluded. `--delete` is opt-in;
excluded destination files remain protected. Identical paths do nothing; nested
source/destination paths are rejected.

## Download Windows Unreal from Linux

Install [Legendary](https://github.com/legendary-gl/legendary) in your chosen environment,
put `legendary` on PATH and run `legendary auth` once. The helper uses its normal CLI:

```bash
python3 Tools/download_ue_windows.py --target-dir '/mounted/drive/Epic Games/UE_5.8' --dry-run
python3 Tools/download_ue_windows.py --target-dir '/mounted/drive/Epic Games/UE_5.8'
```

`--include-symbols` adds debug symbols. Dry run prints commands without network or
login. The helper populates UE metadata and calls `legendary install`; it does not
inject Python package paths or edit Epic's `LauncherInstalled.dat`. Use `UE_ROOT`
or `-UePath` for a nonstandard install. Epic Games Launcher on Windows remains an
alternative.

## Public GitHub sync

```bash
bash Tools/sync_github.sh                # preview committed changes using cached github/main
bash Tools/sync_github.sh --push         # fetch, prepare every commit, then push
```

Individual source commits retain their authors, dates and messages. Mixed commits
are filtered using `.gitignore.github`; original-game content and excluded generated
assets are removed from each exported tree. A source-SHA trailer and patch IDs recognize
new exports and older cherry-picks. Content-only and already-applied commits are skipped.

The script uses temporary Git indexes instead of switching branches or checking out
thousands of assets. It leaves the working files, real index and local branches alone.
Preview creates unreachable Git objects for inspection, but changes no refs and does
not push. Uncommitted edits are never included. Conflicts or merge commits stop the
operation; merges require manual handling rather than silently dropping changes.
`--push` uses a normal fast-forward push, with no force option.

## Formatting and tests

clang-format reads the project's `.clang-format` automatically. It does not recursively
find files itself; Bash can supply the source files in one command:

```bash
find Source -type f \( -name '*.h' -o -name '*.cpp' \) -exec clang-format -i {} +
clang-format --dry-run --Werror Source/DesktopForces/UI/DFMenu.cpp
python3 -m unittest discover -s Tools/tests -v
```

`Tools/tests/test_tools.py` checks path/configuration and launch regressions;
`test_sync.py` tests real Git and rsync behavior in disposable directories. Neither
runs during normal play or packaging. The native `DesktopForces.Gameplay.Options`
automation test runs inside a standalone/listen game and covers settings and gameplay
state transitions.
