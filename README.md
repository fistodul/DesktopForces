# Desktop Forces

A team shooter for low-end PCs in the spirit of **Mobile Forces** (Rage Software, 2002), made
in Unreal Engine 5.8 by the Mobile Forces community ([mf.nofisto.com](https://mf.nofisto.com)).
It plays like the original (rules and numbers are ported from its UnrealScript) and can play
its maps, but it is its own game: code, tools, docs and art in this repository are ours.

- **No original game data in this repository.** A Python pipeline (`Tools/mfport`) converts the
  maps, textures, sounds and models from **your own copy** of Mobile Forces into a separate
  content plugin, *Mobile Forces Classic* (`Plugins/MobileForcesClassic`). Disable that plugin
  and the game builds without any of it.
- **Community content** whose authors agreed (list in [`Tools/mfport/packs.toml`](Tools/mfport/packs.toml))
  is part of the game itself; textures it still borrows from the original game get replacement
  art over time ([Docs/Pipeline.md](Docs/Pipeline.md#content-packs)).
- **Low-end first:** forward shading, baked lighting, no Nanite / Lumen / virtual shadow maps,
  textures at most 512 px (256 on low), 4 GB VRAM is plenty ([Docs/Performance.md](Docs/Performance.md)).
- Gameplay numbers carry the original value in a comment ([Docs/Gameplay.md](Docs/Gameplay.md)).

## Quick start (new teammates)

Cloned the repo and only see a grey test range with a ball? That's `MF_TestRange` — the
built-in physics sandbox. The actual game content (maps, weapons, vehicles, textures) needs
to be imported from your Mobile Forces installation.

**Easiest way:**
```bash
python3 Tools/setup.py            # interactive: checks prereqs, sets up config, picks maps
```

Or list/import maps directly:
```bash
python3 Tools/setup.py --list-maps      # show the 22 core maps with authors
python3 Tools/setup.py --import-all     # import everything (takes a while first time)
```

## Quick start (manual)

Requirements: Unreal Engine 5.8, Python 3.11+ with Pillow and numpy, Wine (Linux/macOS only;
runs the original `UCC.exe` exporter for levels and sounds), ffmpeg (sounds, music), a Mobile
Forces install for the classic content.

```bash
# 1. point the pipeline at your game (optional if the default path fits)
cp Tools/mfport/mfport.example.toml Tools/mfport/mfport.toml && $EDITOR Tools/mfport/mfport.toml

# 2. build the C++ modules
Tools/play.sh build

# 3. convert + import a map (warehouse ~10 min the first time; shaders compile once)
python3 Tools/mfport/mfport.py all mf-warehouse

# 4. play (map, mode dm|tdm|holdout|sc|ctf|det|cap|trailer|koh|tug, bots) - or `Tools/play.sh editor`
Tools/play.sh mf-warehouse tdm 8

# or start at the main menu (map / mode / bots / options / credits)
<UE>/Engine/Binaries/Linux/UnrealEditor "$PWD/DesktopForces.uproject" -game
```

On Windows: `Tools\build_windows.ps1` builds and packages, and the pipeline runs with `py -3`
instead of Wine - [Docs/Building.md](Docs/Building.md#windows), also for dual boot (one
checkout shared by Linux and Windows).

Default controls are the original ones (WASD, mouse, Q loadout, R reload, 1-0 weapons,
Ctrl / C crouch (nitro when driving), Shift walk, E get in / out of vehicles, G drop the flag,
V orders (keys or mouse), F1 scores, Tab one-line console, `~` full console, Esc menu), plus X for armour/gadgets; in the loadout screen a number picks a row, a second
number the item in it. Map URL options: `?game=/Script/DesktopForces.TeamDeathMatch` (also
`Holdout`, `KingOfTheHill`, ...), `?Bots=8?BotSkill=0.7`, `?Teams=4`,
`?FragLimit=10?TimeLimit=5`, `?Vehicles=0`.

## Layout

| Path | What |
|---|---|
| `Source/DesktopForces/` | Game module: player, movement, weapons (one file each), loadout, game modes, vehicles, HUD, menu |
| `Source/DesktopForces/Game/<Mode>/`, `Weapons/<Name>/`, `Vehicles/<Name>/` | The MF-mods (game modes, weapons and items, vehicles), part of the game - list in [Modding](Docs/Modding.md) |
| `Source/DesktopForcesEditor/` | Editor module: importer commandlet (`-run=DFImport`) that builds assets and maps from the pipeline output |
| `SourceArt/` | The team's own art and music (menu pictures, start-up theme, mod icons) - see [CREDITS.md](CREDITS.md) |
| `Tools/artgen/` | Procedural textures + SourceArt conversion |
| `Tools/mfport/` | Python pipeline: reads the original packages (`.umf/.utx/.uax/.u`), exports with UCC, converts BSP/meshes/textures/sounds |
| `Tools/mfport/packs.toml` | Which converted content is part of the game and which stays classic |
| `Tools/mfport/overrides/` | Replacement textures (committed; take precedence over the originals) |
| `Content/MF/` | Our own assets (master materials, menu art, crate, test map) - committed |
| `Content/Maps/`, `Content/MF/Community/` | Converted community maps cleared for the game (tracked on Azure, generated on GitHub) |
| `Plugins/MobileForcesClassic/` | Content converted from the original game - generated locally, **never committed** |
| `Tools/setup.py` | Interactive first-time setup and map import helper |
| `Tools/sync_github.sh` | Sync source-only code from `main` → GitHub (strips content) |
| `Docs/` | How everything works |

## Docs

- [Architecture](Docs/Architecture.md) - modules, classes, how MF concepts map to UE5
- [Pipeline](Docs/Pipeline.md) - asset conversion, content packs, file formats, overrides
- [Gameplay reference](Docs/Gameplay.md) - every number taken from the original game
- [Performance rules](Docs/Performance.md) - low-end rendering and code rules
- [Modding](Docs/Modding.md) - adding weapons, modes and maps; content after release
- [Console commands](Docs/Commands.md) - MF commands, admin, cheats, test helpers
- [Building and packaging](Docs/Building.md) - from source, builds that start at the main menu
- [Hosting a server](Docs/Hosting.md) - listen / dedicated servers, match options, admin
- [Roadmap](Docs/Roadmap.md) - what's done and what's next

## Licence

Code, tools and docs: Mozilla Public License 2.0 - see [LICENSE.md](LICENSE.md). Art and music
in `SourceArt/` belong to their authors ([CREDITS.md](CREDITS.md)).

Mobile Forces is © Rage Software and its current rights holders. Desktop Forces is a fan-made
game, not affiliated with them, and ships no original game data.
