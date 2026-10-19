# Mobile Forces — Unreal Engine 5 remake

A remake of **Mobile Forces** (Rage Software, 2002, Unreal Engine 1.5) in Unreal Engine 5.8,
built mobile-first so it also runs well on low-end PCs.

- **Code, tools and docs are open source.** The original game's assets are *not* in this
  repository. A Python pipeline (`Tools/mfport`) converts them from **your own copy** of
  Mobile Forces (Steam) into the project. Legally clean replacements can be dropped in
  over time (see [Docs/Pipeline.md](Docs/Pipeline.md#overrides)) until the game no longer
  needs the original files at all.
- Gameplay is ported from the original UnrealScript: numbers and rules come from the
  decompiled classes. Every constant carries a comment with the original value
  ([Docs/Gameplay.md](Docs/Gameplay.md)).

## Quick start

Requirements: Unreal Engine 5.8, Python 3.11+ with Pillow, Wine (Linux/macOS only; used
to run the original `UCC.exe` exporter), ffmpeg (optional, sounds), a Mobile Forces install.

```bash
# 1. point the pipeline at your game (optional if the default path fits)
cp Tools/mfport/mfport.example.toml Tools/mfport/mfport.toml && $EDITOR Tools/mfport/mfport.toml

# 2. build the editor
<UE>/Engine/Build/BatchFiles/Linux/Build.sh MobileForcesEditor Linux Development -project="$PWD/MobileForces.uproject"

# 3. convert + import + light a map (warehouse ~10 min the first time; shaders compile once)
python3 Tools/mfport/mfport.py all mf-warehouse

# 4. play
<UE>/Engine/Binaries/Linux/UnrealEditor MobileForces.uproject /Game/MFMaps/Generated/mf-warehouse -game
```

Default controls are the original ones (WASD, mouse, Q loadout, R reload, 1-0 weapons,
C crouch, Shift walk, F1/Tab scores), plus X for armour/gadgets; in the loadout screen a
number picks a row, a second number the item in it. Map URL options: `?game=/Script/MobileForces.MFGameMode_TDM`
(also `MFGameMode_Holdout`, `/Script/MFModKOH.MFGameMode_KOH`), `?Bots=8?BotSkill=0.7`,
`?Teams=4`, `?FragLimit=10?TimeLimit=5`.

## Layout

| Path | What |
|---|---|
| `Source/MobileForces/` | Game module: player, movement, weapons, loadout, game modes, HUD |
| `Source/MobileForcesEditor/` | Editor module: importer commandlet (`-run=MFImport`) that builds assets and maps from the pipeline output |
| `Tools/mfport/` | Python pipeline: reads the original packages (`.umf/.utx/.uax/.u`), exports with UCC, converts BSP/meshes/textures/sounds |
| `Tools/mfport/overrides/` | Replacement textures/sounds (committed; take precedence over the originals) |
| `Plugins/Mods/` | Mods as UE plugins (Molotov, King of the Hill, …) — see [Docs/Modding.md](Docs/Modding.md) |
| `Content/MF/` | Our own assets (master materials, ...) — committed |
| `Content/MFImported/`, `Content/MFMaps/Generated/` | Generated from the original game — **never committed** |
| `Docs/` | How everything works |

## Docs

- [Architecture](Docs/Architecture.md) — modules, classes, how MF concepts map to UE5
- [Pipeline](Docs/Pipeline.md) — asset conversion, file formats, overrides
- [Gameplay reference](Docs/Gameplay.md) — every number taken from the original game
- [Performance rules](Docs/Performance.md) — mobile-first rendering and code rules
- [Modding](Docs/Modding.md) — mods as plugins, examples, data formats
- [Roadmap](Docs/Roadmap.md) — what's done and what's next

## Status

Early but playable: deathmatch, team deathmatch, Holdout and King of the Hill on converted
original maps with the full stock arsenal, the loadout system and bots. See the roadmap for
the rest.

## Licence

Code, tools and docs: GNU LGPL v3 or later, with an additional permission for linking with
Unreal Engine — see [LICENSE.md](LICENSE.md). Mods keep their own licences.

Mobile Forces is © Rage Software / Majesco. This project is a non-commercial fan remake;
it ships no original game data.
