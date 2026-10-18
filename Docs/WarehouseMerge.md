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

## Steps
1. **VMF converter** (`Tools/mfport/mflib/vmf.py`, todo): solids → convex polyhedra by plane
   intersection, Valve `uaxis/vaxis` → UVs, displacements, `func_detail` merged into world.
   Output the same `.mfmesh` + `level.json` as the UMF path so the importer is shared.
2. **Material mapping table** (`Tools/mfport/vmf_materials.toml`): every VMF material → an MF
   texture key (`rage_warehouse.*`), a CC0 replacement, or a team-made texture. `materials/warehouse/*`
   in MFS are MF textures already; HL2 ones (`BRICK/BRICKFLOOR001A`, `metalwall030a` …) must be remapped.
3. **Props**: MFS models in `models/warehouse/*.mdl` → decompile (Crowbar) → SMD → `.mfmesh`.
   HL2 props are replaced by MF static meshes or new CC0/team models.
4. **Alignment**: pick ~10 landmarks present in both (flag bases, holdout, locker rooms,
   canal, containers), solve scale + 90° rotation + offset by least squares. Expected scale
   ≈ MF uu / Source unit ≈ 1.0–1.1 (both games size players ~72–78 units).
5. **Area by area**: split both maps by MF zone (the BSP gives zone bounds). For each area
   choose MFS geometry (extended/more detail) or original, fix seams with new brushwork.
   The team decides per area — a side-by-side screenshot sheet per zone will be generated
   to make the choices.
6. **Gameplay actors** come from the original map (every mode works), plus MFS extras:
   secrets, `item_*` → MF pickups, `prop_vehicle_jeep` → MF Buggy generator,
   `npc_zombie*` → ZombieGame easter-egg spawns (own models).
7. Lighting re-baked with Lightmass; result saved as the canonical `mf-warehouse`.
