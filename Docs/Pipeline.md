# Asset pipeline (`Tools/mfport`)

Converts the original game (your install) into UE assets. Two halves:

1. **Python** (`Tools/mfport/mfport.py`, library in `mflib/`) reads/export the original
   data and writes neutral intermediate files to `Tools/mfport/work/` (git-ignored).
2. **C++ commandlet** (`DesktopForcesEditor`, `-run=DFImport`) turns those into UE assets
   and levels. `mfport.py import` runs it.

```
Mobile Forces install ──UCC.exe──► work/raw  (BMP, WAV, T3D)   (Wine on Linux / macOS)
        │                                    │
        └── mflib/upkg.py + bsp.py ──────────┴──► work/out  (PNG, WAV, .mfmesh, level.json)
                                                        │
                              UnrealEditor-Cmd -run=DFImport
                                                        ▼
      Plugins/MobileForcesClassic/Content/…   (original game: textures, sounds, stock maps …)
      Content/Maps/<map> + Content/MF/Community/…   (community maps cleared for the game)
```

## Steps

| Command | Does |
|---|---|
| `mfport.py map mf-warehouse` | BSP world geometry → chunked `.mfmesh`; static meshes → `.mfmesh`; actors → `level.json` |
| `mfport.py props` | gameplay meshes from Rage.u / RageGame.u / RageWeapons.u (`DeviceMesh`: vehicles, wheels, crates, safes, flag bases, det locks, holdout timers; `LodMesh`: weapon models, rockets, det key) → `.mfmesh` |
| `mfport.py textures` | every texture used by converted maps → PNG (≤ `max_texture_size`), read straight from the packages (see below) |
| `mfport.py effects` | MF sprite particles (`mflib/effects.py`): the effect textures' AnimNext chains → flipbook atlases, plus emitter descriptions for the scripted effects (RageEffects.Explosion, VehicleExplosion, ExplosionFire, EffectsFix smoke puffs, wall hits, blood) and every map's ParticleEffect / FireyExample → `Effects/effects.json`. Run after `map` |
| `mfport.py skeletal` | skeletal meshes with their animations → glTF (`Skeletal/<package>/*.glb`, `skeletal.json`): the soldiers (RageGfx `RagePlayer1Mesh` helmet, `RagePlayer2Mesh` balaclava, 302 animations each) and every first-person weapon (`<X>PlayerMesh` in the weapon packages); the importer's `skeletal` step brings them in through Interchange (`<pack>/Soldiers/…`, `<pack>/Weapons/…`) and puts MF's textures (team skins) on |
| `mfport.py sounds [pkg…]` | exports sound packages (default: weapon/player/announcer + map ambience), WAV → 16-bit PCM |
| `mfport.py music [pkg…]` | tracker music (`.umx`) → WAV |
| `mfport.py packs` | lists each converted map's [content pack](#content-packs) and what core maps still use from the original game |
| `mfport.py vmf mfs-warehouse` | Source (Hammer) map from `vmf_maps.toml` → same formats, aligned to the MF map; MF-derived props from compiled `.mdl` |
| `mfport.py merge mf-warehouse` | builds `mf-warehouse-merged` from the original + MFS by the picks in `merge/mf-warehouse.toml` |
| `mfport.py import [map…]` | runs `Tools/artgen` and the UE importer (`--steps art,textures,props,effects,skeletal,sounds,music,maps`; maps also get their navmesh baked; `effects`: flipbooks, particle materials, `/MobileForcesClassic/Effects/P_<Name>` systems). `--steps patch` refreshes the zone actors (water / pain / current volumes, loadout zones, captain boxes), the road graph, teleporters, periodic sounds, placed pickups, movers, blockers (invisible walls, spawn room doors) and particle effects of already built maps in place (no rebuild, the bake stays) |
| `mfport.py light [map…]` | bakes Lightmass lighting + precomputed visibility (`ResavePackages -buildlighting`) |
| `mfport.py all mf-warehouse` | everything above |

Configuration: `Tools/mfport/mfport.toml` (copy `mfport.example.toml`) or `MFPORT_<KEY>` env vars.

## How the original data is read

- **Package format**: MF is UE1 build 436 with Rage changes; package version 83.
  `mflib/upkg.py` reads names/imports/exports and tagged properties.
- **UCC** (under Wine on Linux / macOS, directly on Windows) does every export it can
  (textures, sounds, static meshes, whole levels as T3D). UCC splits its own command line at
  spaces, so it's started through a space-free path: a symlink (`~/.cache/mfport/game`) with
  `$PWD` set to it (Wine reads `$PWD`), on Windows the folder's 8.3 short name.
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
  through fake-backdrop polygons. `ADFSkyCapture` (spawned at the SkyZoneInfo) captures a
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
  the mesh-import scale already applied. Converted to `/Game/DFImported/Props/<Package>/<Name>`;
  game actors put them on with `DF::ApplyPropMesh` and keep their placeholder shapes without.
- **LodMesh** (`mflib/lodmesh.py`): UE1 vertex-animated mesh (packed 11/11/10-bit vertices per
  frame, wedges with byte UVs, faces, materials, mesh origin/rotation/scale). The first frame
  becomes a static prop: weapon carry models (held in first person, dropped on the floor),
  rockets, grenades, the detonation key.
- **SkeletalMesh** (`mflib/skelmesh.py`): the LodMesh part (faces, wedges, materials), then the
  reference pose (points), the skeleton (`.psk` convention: the root's quaternion as stored,
  every other bone's conjugated, world = parent × local), each bone's run of influences
  (point, weight / 65535) and every influence's point in that bone's space. Rebuilding the
  points from the bones matches to 0.006 uu, which pins the conventions down. Written as glTF
  (`.glb`) in actor space, cm (glTF is metres, Y up: the exporter does the inverse of
  Interchange's axis swap), with the mesh's ORIGIN baked in the way UE1 draws skeletal meshes:
  turned by the *inverse* RotOrigin and mirrored in Y against the .psk (only so do soldiers
  face where they walk with the gun in the right hand, and the first-person arms reach
  forward). A non-uniform MESHMAP SCALE (first-person weapons: 1, 0.5, 1) can't live in a
  skeleton: its rest is left for the mesh component (`UDFMeshScaleData` on the asset). Repeated
  bone names are numbered. Animations become glTF animations (keys are seconds; looping
  sequences get a closing key back to the first frame).
- **Static meshes**: embedded in the level T3D (inside the first StaticMeshActor using them);
  triangles have position, normal and normalised UV.
- **Animations** (`mflib/anims.py`): `MeshAnimation` objects parse fully (bones, motion
  chunks, sequences with one group name, frame counts, rates, notifies); weapon timings come
  from them, and `skelmesh.py` converts them with their meshes.

## Units

1 MF unit = **2.25 cm** (`mflib/config.py: UU_TO_CM`). The MF player capsule (17 × 39)
becomes the UE5 default capsule. The pipeline converts all positions and distances; C++
gameplay code uses plain cm values with the MF original in a comment.
Coordinate systems match (both left-handed, X forward, Z up) — no axis swaps.
Rotations: UE1 units (65536 = 360°) → degrees.

## Intermediate formats

### `.mfmesh` (`mflib/mfmesh.py`)
`<name>.mfmesh.json` (materials, sections, counts, lightmap resolution, collision mode,
render flag) + `<name>.mfmesh.bin` (float32 interleaved `pos3 nrm3 uv2`, or `pos3 nrm3 uv2 col3`
for world geometry: the polygon's zone ambient, then uint32 indices).
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

### `Effects/effects.json` (`mflib/effects.py`)
`flipbooks`: texture key → atlas PNG, frame count, columns × rows, the first frame's size.
`systems`: name → emitter list. An emitter is RageSprite.AddOne's data in UE1 units: texture,
draw type (`add`, `sub`, `alpha`), alpha, size (× the texture's width), life, a burst / a rate
(on `period` s, off `interval` s, `phase`) / rate keys, location + random offset, velocity +
random + radial, flags (anim once, fade, gravity, collide, stretch, rotate). Maps' emitters are
the same records (`particle_effect` in `spawns`). The importer makes Cascade particle systems
from them: plain data, no editor needed, CPU sprites that stop simulating off screen
(`SecondsBeforeInactive`); subtractive smoke is drawn as the scene times 1 − texture.

Material keys are `package.group.texture|mode` (mode: `opaque`, `masked`, `additive`,
`modulated`, `sky`, + `_unlit`, `_2s`).

## Lights

UE1 colour = `FGetHSV(LightHue, LightSaturation, LightBrightness)` (saturation 255 = white),
radius = 25 × (LightRadius + 1) uu. Converted to static UE lights with non-inverse-square
falloff (exponent 2). `LIGHT_INTENSITY` / `SUN_INTENSITY` in `actors.py` = 3.0 (≈ π: UE's
Lambert divides by π, so a full-brightness light shows the texture at its own brightness).

- **Ambient and bounce light** (UE-native, all baked, free at runtime): UE1 added each zone's
  `FGetHSV(AmbientHue, AmbientSaturation, AmbientBrightness)` to every surface, unshadowed.
  Now the map's main ambient (area-weighted over all zones; zones without a ZoneInfo use the
  LevelInfo's, which is how MF-Filip lights everything) is Lightmass's environment colour (light
  arriving through sky windows), Lightmass bounces light twice (`LIGHTMASS_BOUNCES` in
  `level.py`), and the lit masters add a small unshadowed floor:
  `BaseColor × lerp(MPC_MF.Ambient, VertexColor.rgb × MPC_MF.AmbientScale, VertexColor.a)`.
  World geometry carries its zone's ambient in the vertex colour (alpha 1), props and actors
  (alpha 0) use the map's main ambient. `AmbientScale` = `AmbientFloor` (0.35, `DefaultGame.ini`)
  × the player's Brightness, set by `ADFMapInfo`. Per-zone light this way costs nothing: no
  extra meshes or draw calls (earlier versions split chunks per zone). Lightmass ignores
  emissive (`EmissiveBoost 0`: UE1 unlit surfaces never lit anything).
- **Sky windows** (fake backdrop) are their own chunks (`Sky_x_y`) that cast no shadow, so the
  sun and the sky colour come in through them (UE1 sunlight lit what could see the sky).
- **Soft shadows**: point/spot lights get a 40 cm source radius, the sun a 3° angle (UE1
  lightmaps had one texel per 32 uu, so its shadows were soft too).
- The builder assigns light properties *before* making a light Static: UE's `Set*()`
  functions do nothing on a registered Static component (`AreDynamicDataChangesAllowed`).
  That bug left every light at UE's default 5000 and made all earlier bakes white.
- Check a map's lights in game: `getall PointLightComponent Intensity`; what the view looks
  at (meshes, materials, lightmap coefficients): `DFTraceMat`.

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

## Our own art (`Tools/artgen`, `SourceArt/`)

`python3 Tools/artgen/artgen.py` writes `Tools/artgen/out/<folder>/` (git-ignored; `mfport.py
import` with the `art` step runs it), and the importer brings everything to `/Game/MF/<folder>/`:

- procedural textures made for this project (no game art): `Art/T_MF_Crate` (loadout crates;
  alpha = stencil painted in the team colour by `M_MF_Crate`)
- `SourceArt/` (committed): the team's own files - menu pictures (`UI/`), the start-up theme
  (`Music/`, mp3/ogg/flac → WAV with ffmpeg), MF-mods icons (`Mods/Icons/`). 8-bit BMPs keep
  the UE1 rule: palette index 0 is transparent. Authors in [CREDITS.md](../CREDITS.md).

`UI/` and `*/Icons/` become UI textures (no mips); the rest world textures (512 max, 256 low).

## Content packs

Converted content is split so Desktop Forces can be built without the original game
(`Tools/mfport/packs.toml`, applied by `mflib/packs.py`, which writes `work/out/packs.json`
for the importer):

| Pack | Root | What |
|---|---|---|
| classic | `/MobileForcesClassic` (plugin) | everything from the retail game; community maps and packages not cleared yet |
| core | `/Game/Maps/<map>`, `/Game/MF/Community` | community maps whose authors agreed, their own texture packages |
| replaced | `/Game/MF/Replacements` | textures with replacement art in `overrides/` |
| unverified | `/MobileForcesUnverified` (plugin) | community mod packages whose authors / asset sources aren't confirmed yet (`[unverified]`: the servers' weapon mods ...); local only like classic, left out of `Tools/package.sh --public`; moved to `[core.packages]` once cleared |
| ours | `/Game/MF` | generated art (`Tools/artgen`: crate, decals, DeBall goal net), materials, meshes made by the importer (`SM_MF_Ring`) |

Core maps get their own material instances in `/Game/MF/Community/Materials`: they point at
the original texture until replacement art exists, so adding an override later only re-points
materials (re-run `import --steps textures` for the map's textures) - no map rebuild, no
re-bake. `mfport.py packs` shows what each core map still borrows (e.g. mf-Toreford: one
`car_park` texture; MF-Filip: 43 from six retail packages).

## Map edits (`Tools/mfport/map_edits.toml`)

Changes a map carries from conversion on, where MF servers changed maps at run time with mods:
vehicle generators the servers added (mutPack addVehicles' spots: helis, pipers, boats, jeeps)
and swapped (BR-thebigisland's truck is a heli). `mflib/mapedits.py` applies them to the
actor list before anything is built; `import <maps> --steps patch` re-places a built map's
vehicle generators without re-baking. Server admins can still add / swap vehicles at run time
(`AddedVehicles` / `VehicleSwaps`, [Hosting.md](Hosting.md)).

## Overrides

The way out of depending on the original art:

- `Tools/mfport/overrides/textures/<package>/<group>/<name>.png` replaces that texture (any
  resolution; UVs always use the original size).
- `Tools/mfport/overrides/sounds/<package>/<name>.wav` replaces that sound.

Overrides are committed and become core content (`/Game/MF/Replacements`). Prefer CC0 sources
(ambientCG, Poly Haven, Kenney, freesound CC0) and note the source/licence in
`overrides/CREDITS.md`.

**Generated stand-ins** (`Tools/artgen/lookalike.py`): for every texture core maps still borrow,
a procedural replacement - bricks, planks, corrugated sheet, siding, tiles, panels, plaster /
concrete noise, pipes, grates (see-through), windows, lamps, glows, screens, skies, and signs
with words of our own. `--auto` picks the generator from the name and from stripes / grids found
in the original (FFT of its rows and columns), and stores it with the original's dark / mid / light
colours in `lookalikes_auto.toml` (committed); `lookalikes.toml` holds hand-tuned entries. Only
colours are taken from the originals. Outputs land in `work/generated/textures` (git-ignored,
re-created anywhere from the tables, no game needed) and count as replacement art after any
hand-made override. See-through shapes (foliage, decals) are left for hand-made art (listed at the
end of the auto table).

    python3 Tools/artgen/lookalike.py --auto    # after converting more core maps
    python3 Tools/artgen/lookalike.py           # generate (7 min for ~850)
    python3 Tools/mfport/mfport.py textures && python3 Tools/mfport/mfport.py import --steps textures

Masked textures get their see-through texels' colour bled in from the opaque neighbours
(`bleed_alpha` in `textures.py`): UE1 kept palette index 0 (white, magenta) there, which
filtering and mips smeared into white fringes (tyres, fences).

## Reading a mod's code

Mod packages (`System/<mod>.u`) keep their UnrealScript source; UCC writes it out:

```python
# from Tools/mfport
from pathlib import Path
from mflib import config, ucc
ucc.batch_export(config.load(), 'System/improvedRL.u', 'Class', 'uc', Path('work/uc/improvedRL'))
```

That is how the servers' mods (improvedRL, OPWeapons, BoxPhysics, FNG ...) were read for porting;
code is ported only with its author's agreement (`packs.toml`, [CREDITS.md](../CREDITS.md)).

## Debugging a conversion

`work/out/Maps/<map>/` holds everything the importer reads. To eyeball geometry without
UE, dump OBJ via `MeshBuilder.write_obj` and render it in Blender (see the snippet in
`Tools/mfport/README.md`).
