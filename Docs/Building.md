# Building and packaging

For the team: get the project running from source, then make builds that start at the main
menu to try out or hand around.

## What you need

- **Unreal Engine 5.8** - the Epic Games Launcher install (Windows) or Epic's Linux binary
  build. No engine source needed.
- **Python 3.11+** with Pillow and numpy, **ffmpeg**, and on Linux / macOS **Wine** (the
  pipeline runs the original game's `UCC.exe` to export levels and sounds).
- **Your own Mobile Forces install** for the classic content (stock maps, textures, sounds,
  models). It is converted on your machine into `Plugins/MobileForcesClassic` and never
  committed or shared.

## From source

```bash
cp Tools/mfport/mfport.example.toml Tools/mfport/mfport.toml   # game path, engine path
Tools/play.sh build                                            # the C++ modules
python3 Tools/mfport/mfport.py all mf-warehouse                # convert + import one map (~10 min)
python3 Tools/mfport/mfport.py all                             # or every map (hours; once)
Tools/play.sh mf-warehouse tdm 8                               # play from the editor build
Tools/play.sh editor                                           # or open the editor
```

Re-running a step after a pull: `mfport.py import --steps art` (generated art, decals, goal
rings), `mfport.py map <maps>` then `import <maps> --steps patch` (map data: zones, generators,
`Tools/mfport/map_edits.toml`) - [Pipeline.md](Pipeline.md) lists every step.

## Packaged builds

A packaged build is the game alone (no editor): it starts at the main menu, maps are picked
there, Options are saved per user.

| Where | Command | Output |
|---|---|---|
| Linux | `Tools/package.sh` (Shipping) / `Tools/package.sh Linux Development` (console, logs) | `Saved/Packages/Linux-<Config>/Linux/DesktopForces.sh` |
| Windows | `powershell -ExecutionPolicy Bypass -File Tools\build_windows.ps1` | as the script prints |

Both cook every map (core and, if converted, classic), everything under `/Game/MF` and
`/MobileForcesClassic` (effects, decals, sounds and props are loaded by name at run time), and
pak them. The first package compiles every shader once: expect an hour or more; later ones
reuse the cache.

**Classic content stays private.** A build made with `Plugins/MobileForcesClassic` contains
content converted from your copy of Mobile Forces: share it only inside the team.
`Tools/package.sh --public` builds without it (the plugin is switched off for that build
only). Core maps that still borrow classic textures then show them missing -
`mfport.py packs` lists which ones need replacement art.

## Checking a build

- Start it: the main menu should come up with the team's picture and theme.
- Play: pick a map and mode, Start; Esc opens the in-game menu.
- A Development build has the `~` console and writes `Saved/Logs/DesktopForces.log` next to
  the game; Shipping has neither.
- Hosting a match for others: [Hosting.md](Hosting.md).
