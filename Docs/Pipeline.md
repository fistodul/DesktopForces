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
| `mfport.py props` | gameplay meshes from Rage.u / RageGame.u (`DeviceMesh`: vehicles, wheels, crates, safes, flag bases, det locks, holdout timers) → `.mfmesh` |
| `mfport.py textures` | every texture used by converted maps → PNG (≤ `max_texture_size`), read straight from the packages (see below) |
| `mfport.py sounds [pkg…]` | exports sound packages (default: weapon/player/announcer + map ambience), WAV → 16-bit PCM |
| `mfport.py music [pkg…]` | tracker music (`.umx`) → WAV |
| `mfport.py mods` | mod plugins' `SourceArt/` → PNG/WAV for `import --steps plugins` |
| `mfport.py vmf mfs-warehouse` | Source (Hammer) map from `vmf_maps.toml` → same formats, aligned to the MF map; MF-derived props from compiled `.mdl` |
| `mfport.py merge mf-warehouse` | builds `mf-warehouse-merged` from the original + MFS by the picks in `merge/mf-warehouse.toml` |
| `mfport.py import [map…]` | runs the UE importer (`--steps art,textures,props,sounds,music,plugins,maps`; maps also get their navmesh baked) |
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
  unlit; two-sided; fake backdrop → sky window (`M_MF_Sky`, see below).
- **Watertight, consistently wound** (`mflib/polyfix.py`): BSP cuts leave T-junctions (a
  vertex in the middle of a neighbour's edge) that a GPU shows as pixel cracks into the void;
  those vertices are welded into the neighbours' edges. Facing comes from the Newell normal
  of all vertices (after BSP splits the first three are often collinear, which flipped ~500
  warehouse polygons: "holes that only work one way"). Triangulation fans from an unsplit
  corner, else from the centroid, so every welded vertex joins real triangles.
- **Sky**: UE1 drew the sky zone (seen from the SkyZoneInfo with the player's rotation)
  through fake-backdrop polygons. `AMFSkyCapture` (spawned at the SkyZoneInfo) captures a
  256 px cube once at load (re-captured at 0.5/2/5/10 s while textures stream in); `M_MF_Sky`
  samples it by view direction.
- **Static mesh actors**: UE1 draws a mesh at `Location + R·S·(v − PrePivot)`, so placement
  applies `−PrePivot` (rotated and scaled). Without it 137 of 138 warehouse meshes were off by
  up to several metres.
- **Textures** are decoded from the packages, not exported: MF textures are palettised (P8)
  and most also carry an S3TC copy (`bHasComp`, `CompMips`: DXT1/DXT3) - what MF's Direct3D
  renderer drew. That copy wins (true colour; on some posters the P8 copy is a magenta-keyed
  fallback). Exceptions: Rage's HUD art keeps soft alpha in its palettes (MF draws it with
  `STY_Alpha`; DXT1 has only 1-bit alpha), and masked uses keep P8 when only palette index 0
  has the holes. A texture's own `bMasked`/`PolyFlags` add to the surfaces using it (UE1:
  surface flags | texture flags). Formats this can't read fall back to UCC's BMP export.
- **DeviceMesh** (`mflib/devmesh.py`): Rage's own vertex-buffer mesh, no UCC exporter. The
  native data ends with what matters, so it's read from the back: vertex array (compact count,
  44 bytes each: position, pad, normal, UV, 8 runtime bytes), u16 index array, section table
  (material, first index, primitive count, list/strip, vertex range). Positions are in uu with
  the mesh-import scale already applied. Converted to `/Game/MFImported/Props/<Package>/<Name>`;
  game actors put them on with `MF::ApplyPropMesh` and keep their placeholder shapes without.
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
falloff (exponent 2). `LIGHT_INTENSITY` / `SUN_INTENSITY` in `actors.py` = 3.0 (≈ π: UE's
Lambert divides by π, so a full-brightness light shows the texture at its own brightness).

- **Ambient and bounce light** (UE-native, all baked, free at runtime): UE1 added each zone's
  `FGetHSV(AmbientHue, AmbientSaturation, AmbientBrightness)` to every surface, unshadowed.
  Now the map's main zone ambient is Lightmass's environment colour (light arriving through
  sky windows), Lightmass bounces light twice (`LIGHTMASS_BOUNCES` in `level.py`), and the lit
  masters add a small unshadowed floor `BaseColor × MPC_MF.Ambient`, one global value set by
  `AMFMapInfo` = map ambient × `AmbientFloor` (0.35, `DefaultGame.ini`) × the player's
  Brightness. Earlier versions split world chunks per zone and set the ambient per component
  (more draw calls); gone. Lightmass ignores emissive (`EmissiveBoost 0`: UE1 unlit surfaces
  never lit anything).
- **Sky windows** (fake backdrop) are their own chunks (`Sky_x_y`) that cast no shadow, so the
  sun and the sky colour come in through them (UE1 sunlight lit what could see the sky).
- **Soft shadows**: point/spot lights get a 40 cm source radius, the sun a 3° angle (UE1
  lightmaps had one texel per 32 uu, so its shadows were soft too).
- The builder assigns light properties *before* making a light Static: UE's `Set*()`
  functions do nothing on a registered Static component (`AreDynamicDataChangesAllowed`).
  That bug left every light at UE's default 5000 and made all earlier bakes white.
- Check a map's lights in game: `getall PointLightComponent Intensity`; what the view looks
  at (meshes, materials, lightmap coefficients): `MFTraceMat`.

### Baking on Linux (UE 5.8.3)
Only whoever bakes needs this; players get the baked result inside the map.

1. The shipped `UnrealLightmass` asserts at start-up ("The Gamethread can only be tagged on
   the inital thread"). Patch your engine's copy once (keeps a backup, refuses other builds):
   ```
   cd <UE>/Engine/Binaries/Linux && cp -n libUnrealLightmass-Core.so libUnrealLightmass-Core.so.orig && \
   python3 -c "p='libUnrealLightmass-Core.so';o=0x91b851;b=bytearray(open(p,'rb').read());assert b[o:o+6]==bytes.fromhex('0f853f010000'),'different engine build';b[o:o+6]=b'\x90'*6;open(p,'wb').write(b);print('patched')"
   ```
   Undo: `cp libUnrealLightmass-Core.so.orig libUnrealLightmass-Core.so`.
2. `mfport.py light` passes `-messaging` to the editor: commandlets keep UdpMessaging off,
   and Lightmass reports to the editor through it (otherwise: "Timed out waiting for the
   recipient", then Lightmass crashes). It also fails loudly if Lightmass crashes.
3. Stuck runs: kill leftover `UnrealLightmass` processes before baking again.

Windows bakes without any of this.

## Our own art (`Tools/artgen`)

`python3 Tools/artgen/artgen.py` generates textures made for this project (procedural, no
game art, MPL-2.0 like the code) into `Tools/artgen/out/` (committed); `import --steps art`
brings them to `/Game/MF/Art/` and builds the materials using them. Today: `T_MF_Crate`
(loadout crates; alpha = stencil painted in the team colour by `M_MF_Crate`).

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
