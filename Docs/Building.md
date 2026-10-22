# Building and packaging

For the team: get the project running from source, then make builds that start at the main
menu to try out or hand around.

## What you need

- **Unreal Engine 5.8** - the Epic Games Launcher install (Windows) or Epic's Linux binary
  build. No engine source needed. Windows also needs Visual Studio 2022 (or its Build Tools)
  with the "Game development with C++" workload, as for any C++ Unreal project.
- **Python 3.11+** with Pillow and numpy (`pip install pillow numpy`), **ffmpeg** with
  libopenmpt (sounds, tracker music; Windows: e.g. the gyan.dev "full" build), and on Linux /
  macOS **Wine** (the pipeline runs the original game's `UCC.exe` to export levels and
  sounds; Windows runs it directly).
- **Your own Mobile Forces install** for the classic content (stock maps, textures, sounds,
  models). It is converted on your machine into `Plugins/MobileForcesClassic` and never
  committed or shared.

A fresh clone holds the code, the team's own art (`SourceArt/`), the test range
(`/Game/MF/Maps/MF_TestRange`) and the pipeline - no maps. Every map, community ones
included, is converted from your install by `Tools/mfport` (they are big and re-creatable, so
git ignores them: `Content/Maps`, `Content/MF/Community`, `Plugins/*/Content`).

## From source

### Linux

```bash
cp Tools/mfport/mfport.example.toml Tools/mfport/mfport.toml   # game path, engine path
Tools/play.sh build                                            # the C++ modules
python3 Tools/mfport/mfport.py all mf-warehouse                # convert + import one map (~10 min)
python3 Tools/mfport/mfport.py all                             # or every map (hours; once)
Tools/play.sh mf-warehouse tdm 8                               # play from the editor build
Tools/play.sh editor                                           # or open the editor
```

### Windows

```powershell
copy Tools\mfport\mfport.example.toml Tools\mfport\mfport.toml   # set game_dir and ue_root (forward slashes are fine)
powershell -ExecutionPolicy Bypass -File Tools\build_windows.ps1 -Config Development -Package:$false   # editor + game modules
py -3 Tools\mfport\mfport.py all mf-warehouse                    # classic content, one map
```

Then open `DesktopForces.uproject` (or right-click it > *Generate Visual Studio project files*
to work in an IDE). `build_windows.ps1` finds the engine on its own (registry, the launcher's
list, `<drive>:\Program Files\Epic Games\UE_<version>`); `-UePath` names it.

The pipeline is written for both systems (`UCC.exe` started natively through its 8.3 short
path, `UnrealEditor-Cmd.exe`), but so far it has only been run on Linux. If the game's folder
has spaces and the drive keeps no 8.3 names, mfport stops and asks for a path without spaces -
a junction does: `mklink /J C:\MobileForces "D:\Games\Mobile Forces"`. Report anything else
that breaks.

**Dual boot?** Convert on Linux and share the result: one checkout for both systems (below),
so Windows only builds and packages.

### After a pull

Re-running a step: `mfport.py import --steps art` (generated art, decals, goal rings),
`mfport.py map <maps>` then `import <maps> --steps patch` (map data: zones, generators,
`Tools/mfport/map_edits.toml`), `mfport.py sounds` + `import --steps sounds` (a sound package
newly used by code) - [Pipeline.md](Pipeline.md) lists every step.

## Packaged builds

A packaged build is the game alone (no editor): it starts at the main menu, maps are picked
there, Options are saved per user.

| Where | Command | Output |
|---|---|---|
| Linux | `Tools/package.sh` (Shipping) / `Tools/package.sh Linux Development` (console, logs) | `Saved/Packages/Linux-<Config>/Linux/DesktopForces.sh` |
| Windows | `powershell -ExecutionPolicy Bypass -File Tools\build_windows.ps1` (`-Config Development`, `-Public`, `-Package:$false` compiles only, `-DryRun` prints the commands, also with `pwsh` on Linux) | `Build\Windows[-public]\Windows\DesktopForces.exe` |

Both cook every map (core and, if converted, classic), everything under `/Game/MF` and
`/MobileForcesClassic` (effects, decals, sounds and props are loaded by name at run time), and
pak them. The first package compiles every shader once: expect an hour or more; later ones
reuse the cache.

**Classic content stays private.** A build made with `Plugins/MobileForcesClassic` contains
content converted from your copy of Mobile Forces: share it only inside the team.
`Tools/package.sh --public` builds without it (the plugin is switched off for that build
only). Core maps that still borrow classic textures then show them missing -
`mfport.py packs` lists which ones need replacement art.

### Windows from a Linux machine (dual boot)

Nothing in the scripts names a user or a mount: `Tools/winpaths.py` finds the mounted Windows
partition (the one with `Windows/System32`), the Windows user (the only one, the one named like
you, or the last to log in) and the Windows spelling of a path; `WIN_ROOT`, `WIN_USER`,
`WIN_DRIVE` and `WIN_PROJECT` override it.

- **One copy for both** (`Tools/share_with_windows.sh`, dry run unless `--apply`; `--undo`
  brings it back): the checkout moves to `C:\Users\<you>\DesktopForces` and a link takes its
  place on Linux. Both systems build into their own folders (`Binaries/Linux` / `Win64`,
  `Intermediate/Build/...`, `Saved/Config/...`), content and the derived data cache are shared.
  Linux reads and writes NTFS natively (ntfs3); the game runs from there. Keep Windows' Fast
  Startup off, or Linux gets the partition read-only. (The other way round - Windows using the
  Linux disk - needs ext4 drivers or WSL's network share, which Unreal's tools handle badly.)
- **A copy for Windows** (`Tools/sync_to_windows.sh [--dry-run] [dest]`): rsyncs the project
  without the build output, `Saved/` and the pipeline's work folder.
- **The engine** (`Tools/download_ue_windows.py [--dry-run]`): the project's engine version for
  Windows straight from Epic's CDN onto the Windows drive with
  [legendary](https://github.com/derrod/legendary) (`pipx install legendary-gl`, `legendary
  auth`), registered in the launcher's list so `build_windows.ps1` finds it.

Default place `C:\Users\<you>\DesktopForces`, not Documents: Windows builds fail on paths
longer than 260 characters deep in `Intermediate`, and Documents carries Windows' read-only
folder flag, which ntfs3 honours.

A typical dual-boot round: convert and test on Linux, `Tools/share_with_windows.sh --apply`
once, then on Windows `Tools\build_windows.ps1` - same content, nothing copied.

## CI (`.github/workflows/build.yml`)

Run by hand (*Actions > Run workflow*) on a self-hosted Windows runner: Win64 through
`build_windows.ps1`, Linux cross-compiled with Epic's clang toolchain; the packages are
uploaded as artifacts. A clean checkout has no maps, so the runner keeps what isn't in git
(`clean: false`): convert the content once in the runner's work folder (or link a converted
checkout's `Content/Maps`, `Content/MF/Community` and `Plugins/*/Content` there). Without it
the build has only the test range.

## Checking a build

- Start it: the main menu should come up with the team's picture and theme.
- Play: pick a map and mode, Start; Esc opens the in-game menu.
- A Development build has the `~` console and writes `Saved/Logs/DesktopForces.log` next to
  the game; Shipping has neither.
- Hosting a match for others: [Hosting.md](Hosting.md).
