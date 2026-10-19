# Asset pipeline (`Tools/mfport`)

Converts the original game (your install) into UE assets. Two halves:

1. **Python** (`Tools/mfport/mfport.py`, library in `mflib/`) reads/export the original
   data and writes neutral intermediate files to `Tools/mfport/work/` (git-ignored).
2. **C++ commandlet** (`MobileForcesEditor`, `-run=MFImport`) turns those into UE assets
   and levels. `mfport.py import` runs it.

```
Mobile Forces install ──UCC.exe (Wine)──► work/raw  (BMP, WAV, T3D)
        │                                    │
        └── mflib/upkg.py + bsp.py ──────────┴──► work/out  (PNG, WAV, .mfmesh, level.json)
                                                        │
                              UnrealEditor-Cmd -run=MFImport
                                                        ▼
                         Content/MFImported/…  +  Content/MFMaps/Generated/<map>.umap
```

## Steps

| Command | Does |
|---|---|
| `mfport.py map mf-warehouse` | BSP world geometry → chunked `.mfmesh`; static meshes → `.mfmesh`; actors → `level.json` |
| `mfport.py textures` | exports every texture used by converted maps, BMP → PNG (≤ `max_texture_size`), alpha for masked |
| `mfport.py sounds [pkg…]` | exports sound packages (default: weapon/player/announcer + map ambience), WAV → 16-bit PCM |
| `mfport.py music [pkg…]` | tracker music (`.umx`) → WAV |
| `mfport.py mods` | mod plugins' `SourceArt/` → PNG/WAV for `import --steps plugins` |
| `mfport.py vmf mfs-warehouse` | Source (Hammer) map from `vmf_maps.toml` → same formats, aligned to the MF map; MF-derived props from compiled `.mdl` |
| `mfport.py import [map…]` | runs the UE importer (`--steps textures,sounds,music,plugins,maps`; maps also get their navmesh baked) |
| `mfport.py light [map…]` | bakes Lightmass lighting + precomputed visibility (`ResavePackages -buildlighting`) |
| `mfport.py all mf-warehouse` | everything above |

Configuration: `Tools/mfport/mfport.toml` (copy `mfport.example.toml`) or `MFPORT_<KEY>` env vars.

## How the original data is read

- **Package format**: MF is UE1 build 436 with Rage changes; package version 83.
  `mflib/upkg.py` reads names/imports/exports and tagged properties.
- **UCC under Wine** does every export it can (textures, sounds, static meshes, whole
  levels as T3D). UCC splits its own command line at spaces, so it's started through a
  space-free symlink (`~/.cache/mfport/game`), with `$PWD` set to it (Wine reads `$PWD`).
- **World geometry** is *not* re-CSG'd from brushes: `mflib/bsp.py` reads the level's
  compiled BSP (`Model` export). Every BSP node with vertices is a final convex polygon;
  its surface gives texture, poly flags and texture vectors. The layout matches UT v69
  except the zone table (no LastRenderTime float).
  UV = (dot(P − Base, TextureU) + PanU) / USize (and V likewise), USize from the texture package.
- **Poly flags** decide what a polygon becomes:
  invisible → collision-only; non-solid / portal → render-only (zone portals skipped);
  masked → alpha-test material; translucent → additive; modulated → modulate (×2);
  unlit; two-sided; fake backdrop → sky.
- **Static meshes**: embedded in the level T3D (inside the first StaticMeshActor using them);
  triangles have position, normal and normalised UV.
- **Animations** (weapons/characters): `MeshAnimation` objects parse fully (bones, motion
  chunks, sequences with frame counts and rates); used for weapon timings, mesh conversion is TODO.

## Units

1 MF unit = **2.25 cm** (`mflib/config.py: UU_TO_CM`). The MF player capsule (17 × 39)
becomes the UE5 default capsule. The pipeline converts all positions and distances; C++
gameplay code uses plain cm values with the MF original in a comment.
Coordinate systems match (both left-handed, X forward, Z up) — no axis swaps.
Rotations: UE1 units (65536 = 360°) → degrees.

## Intermediate formats

### `.mfmesh` (`mflib/mfmesh.py`)
`<name>.mfmesh.json` (materials, sections, counts, lightmap resolution, collision mode,
render flag) + `<name>.mfmesh.bin` (float32 interleaved `pos3 nrm3 uv2`, then uint32 indices).
Positions in UE cm, mesh-local. Triangle winding: `cross(v1-v0, v2-v0)` points along the
normal; the UE importer reverses it (UE's convention). Simple enough for other tools
(e.g. the planned VMF converter) to write.

### `level.json`
- `world`: geometry chunks (`geo/World_x_y`, `geo/Collision_x_y`) with pivot location.
- `static_meshes`: converted meshes.
- `spawns`: records the C++ builder spawns verbatim — `static_mesh`, `point_light`,
  `spot_light`, `directional_light`, `actor` (any UE class + properties as text),
  `loadout_zone`, `ambient_sound`, `map_info`, `legacy` (original actors kept for future
  game modes: flags, trailer parks, vehicle generators …).
  **All MF→UE actor knowledge lives in `mflib/actors.py`.**
- `actors`: raw converted actor list (debugging), `texture_usage`, `lightmass`.

Material keys are `package.group.texture|mode` (mode: `opaque`, `masked`, `additive`,
`modulated`, `sky`, + `_unlit`, `_2s`).

## Lights

UE1 colour = `FGetHSV(LightHue, LightSaturation, LightBrightness)` (saturation 255 = white),
radius = 25 × (LightRadius + 1) uu. Converted to static UE lights with non-inverse-square
falloff (exponent 2). Zone ambient light → Lightmass environment colour.
`LIGHT_INTENSITY` / `SUN_INTENSITY` in `actors.py` are tuning knobs.

## Overrides

The way out of depending on the original art:

- `Tools/mfport/overrides/textures/<package>/<group>/<name>.png` replaces that texture (any
  resolution; UVs always use the original size).
- `Tools/mfport/overrides/sounds/<package>/<name>.wav` replaces that sound.

Overrides are committed. Prefer CC0 sources (ambientCG, Poly Haven, Kenney, freesound CC0)
and note the source/licence in `overrides/CREDITS.md`.

## Debugging a conversion

`work/out/Maps/<map>/` holds everything the importer reads. To eyeball geometry without
UE, dump OBJ via `MeshBuilder.write_obj` and render it in Blender (see the snippet in
`Tools/mfport/README.md`).
