# mfport — Mobile Forces → UE5 asset pipeline

```bash
python3 mfport.py all mf-warehouse     # convert, import, light
python3 mfport.py map mf-polar         # just convert one map (writes work/out/Maps/mf-polar)
python3 mfport.py textures             # textures used by all converted maps
python3 mfport.py sounds               # weapon/player/announcer + map ambience
python3 mfport.py import --steps maps mf-polar
python3 mfport.py light mf-polar
python3 -m mflib.anims <game>/System/RageWeapons.u   # animation sequence table (weapon timings)
```

Full documentation: [../../Docs/Pipeline.md](../../Docs/Pipeline.md).

## Library (`mflib/`)
| Module | Does |
|---|---|
| `config.py` | settings (`mfport.toml`), `UU_TO_CM` |
| `upkg.py` | UE1/MF package reader: tables, tagged properties |
| `bsp.py` | compiled level BSP → world polygons (+ poly flag constants) |
| `t3d.py` | T3D parser (levels, static meshes) |
| `ucc.py` | runs the game's `UCC.exe batchexport` (Wine on Linux) |
| `level.py` | map conversion driver (geometry chunks, meshes, actors) |
| `actors.py` | MF actor → UE spawn record mapping, UE1 light colours |
| `mfmesh.py` | `.mfmesh` writer (+ OBJ debug dump) |
| `textures.py`, `sounds.py` | export/convert with override support |
| `anims.py` | MeshAnimation reader |
| `ue.py` | runs UE commandlets |

## Previewing a converted map without UE
`debug/level_to_obj.py` merges a converted map into one OBJ+MTL; `debug/blender_render.py`
renders it headless with Blender (top view + given camera spots). Handy to check BSP/UV
conversion before importing.
