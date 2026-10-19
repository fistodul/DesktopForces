# mf-warehouse merge plan

Goal: **one** mf-warehouse that combines the original map (Billy Thomson, Rage) with the
extended Mobile Forces Source remake (`MoFo/Mobile-Forces-Source/mp/game/mobileforcessource/mapsrc/mf-warehouse.vmf`):
MFS's extra geometry, details and secrets, matched area by area to the original layout,
with no Valve/HL2 content.

## What each version has
| | Original (`Maps/mf-warehouse.umf`) | MFS (`mf-warehouse.vmf`) |
|---|---|---|
| Extent | ~9 500 × 11 800 × 1 070 uu (actors) | ~8 000 × 7 400 × 1 500 Hammer units |
| Geometry | 1 169 CSG brushes → 11 748 BSP polys, 61 static meshes | brush solids + 308 prop_static, 396 prop_dynamic, 423 prop_physics |
| Game modes | all 8 (flags, safes, det locks, trailer parks/delivery, holdout, captain starts, crates, vehicle generators, road points) | DM/TDM/captains/holdout entities, MFS-specific |
| Content to drop | — | HL2 materials (`BRICK/…`), HL2 NPCs (zombies, Alyx, combine, hunter), `prop_vehicle_jeep` |

## Status (2026-10-19)

| Step | State |
|---|---|
| VMF converter (`mfport.py vmf mfs-warehouse`) | ✅ brushes → `.mfmesh`, MF textures by name, entities |
| MFS props | ✅ 820 MF props (`models/warehouse/*`, MF meshes recompiled by the MFS team) read from compiled `.mdl/.vvd/.vtx` (`mflib/sourcemdl.py`) · 459 Valve props left out (listed in `level.json` → `skipped_models`) |
| Alignment | ✅ see below |
| Per-area comparison | ✅ team picks in `Tools/mfport/merge/mf-warehouse.toml` |
| Merge build | 🟡 `mfport.py merge mf-warehouse` → map `mf-warehouse-merged` (playable, unlit); seams being fixed |

### Alignment
Landmarks from the team's Hammer screenshots: blue spawn room (1006, −1000), red spawn
room (1072, −1455), `prop_holdout` (−6420, −1521) ↔ the original's team 0 / team 1 starts
and `RageDominationTimer`. Least-squares similarity (with a mirror, because Hammer is
right-handed):

- **scale 1.48** (MFS is ~0.68× the original), **rotation 91.4°**, offset (7500, 1949) uu
- residuals: 75 / 74 / 5 uu; outer walls and catwalks line up across most of the map
- **heights scale differently: 1.159**, offset −42.2 uu - pinned by two storeys (MFS ground
  −48 / base floor 64 → MF −97.8 / 32 uu); the deck lands on MF's 320 uu too
- props: the MFS team recompiled MF meshes at 0.75 → `prop_scale` 1.333 restores MF size

Stored in `Tools/mfport/vmf_maps.toml`; both maps share one frame after conversion.

### Tools
| Tool | Output |
|---|---|
| `debug/overlay.py A B out.png [--zmin --zmax]` | both maps' walkable floors in one image (blue / orange) |
| `debug/merge_areas.py mf-warehouse mfs-warehouse work/merge` | `areas.md`: per original zone, triangles + props of both, floor-plan crops |
| `debug/merge_shots.py mf-warehouse mfs-warehouse work/merge/shots` | in-engine textured top views of every zone, same camera in both maps, side by side |

Outputs stay in `Tools/mfport/work/` (git-ignored: they are renders of the original game).

### Merge (`mfport.py merge mf-warehouse`)
- Every original zone box is a region with a version (picked, or inherited from the nearest
  picked zone); the smallest box containing a point wins. Boxes grow by `margin` (1 m) only
  into space no zone owns (wall interiors), `default` (MFS) beyond the original map.
- World polygons are **clipped** at the region borders; props and lights go by position;
  gameplay actors come from the original, `MFS:*` extras from the remake.
- **Border fill**: a floor the other version had within 1.5 m of a border is kept where the
  merged map has no floor at that height (closes most gaps between the two layouts).
- `warp = true` tries a smooth local alignment (`mflib/warp.py`); it did not reduce seams
  here, so it's off.

### Seams
`debug/seams.py mf-warehouse` rasterises the merged floors and lists border spots where a
floor (≥ 1 m wide) on one side has no floor within a step on the other and no wall closes
the border; `debug/seam_shots.py mf-warehouse-merged --seams <dir>` screenshots them.

| Fix | Seam cells (all / inside the perimeter) |
|---|---|
| first merge | 7517 / – |
| no `default` in wall gaps | 6774 / – |
| height scale 1.159 | 6027 / – |
| wall + width aware detector | 5560 / 1671 |
| margins only in gaps | 4524 / 1055 |
| border fill | **1513 / 447** |

Left (2026-10-19): thin gaps between ground floors near (12100, −150 … −800, −216) cm,
two pits into the void near (8625, −900 / −675) cm, roof edges along the outer walls
(drops, fine). Nav check: 28 of 36 spawns/objectives fully reachable (the original: fewer).

### Choosing areas
Picks live in `Tools/mfport/merge/mf-warehouse.toml` (`MF`, `MFS` or `both`);
`work/merge/areas.md` shows them next to the comparison images. Rule of thumb from the team: take the version with more
detail / geometry / smoother shapes; keep MFS secrets, `func_breakable` doors and passages.
Gameplay actors (spawns, crates, objectives, vehicles) always come from the original so
every game mode works; MFS extras (secrets, buggy spots, zombie easter eggs) are added.

## Steps
1. ✅ **VMF converter** (`Tools/mfport/mflib/vmf.py`): solids → convex polyhedra by plane
   intersection, Valve `uaxis/vaxis` → UVs, `func_detail` merged into world.
   Output: the same `.mfmesh` + `level.json` as the UMF path, so the importer is shared.
   Todo: displacements, `func_breakable` → breakable actor.
2. 🟡 **Material mapping** (`Tools/mfport/vmf_materials.toml`): MF texture names resolve
   automatically; 57 materials (906 faces) still unmapped (HL2 `BRICK/…`, `metalwall…`) →
   MF texture, CC0 replacement or team texture.
3. 🟡 **Props**: MF-derived props done. Valve props (`props_c17` handrails, `props_junk`
   pallets, lab lights, vents, `models/props/box` …) → MF statics or new CC0/team models.
4. ✅ **Alignment** (above).
5. 🟡 **Area by area**: picks done, `mfport.py merge` builds the map; remaining seams
   need small fixes (automatic where possible, else hand brushwork in the editor).
6. ⬜ **Gameplay actors** from the original + MFS extras: `prop_vehicle_jeep` → MF Buggy
   generator, `npc_zombie*` → ZombieGame easter-egg spawns (own models), `item_*` → MF pickups.
7. ⬜ Lighting re-baked; result saved as the canonical `mf-warehouse`.
