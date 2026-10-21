"""Gameplay props from the game packages: Rage `DeviceMesh`es (vehicles and their wheels,
loadout crates, safes, flag bases, detonation locks, holdout timers) and UE1 `LodMesh`es,
first frame (weapon carry/pickup models, rockets, the detonation key ...).

    mfport.py props   ->  work/out/Props/<Package>/<Mesh>.mfmesh.* + Props/props.json

`import --steps props` builds them as /MobileForcesClassic/Props/<Package>/<Mesh>; game actors
use them when present (a converted install) and keep their placeholder shapes otherwise.
"""
from __future__ import annotations

import json
import math
from pathlib import Path

from . import devmesh, lodmesh
from .config import Config
from .level import PF_MASKED, PF_TRANSLUCENT, UU_TO_CM, material_key, _cross, _dot, _sub, _area, _next_pow2
from .mfmesh import MeshBuilder
from .textures import _package_path, texture_index
from .upkg import Package

# + MF-mods packages with weapon models (missing ones are skipped)
PACKAGES = ('Rage', 'RageGame', 'RageWeapons', 'RageEffects', 'deagle', 'grenLaunch', 'battleRoyale', 'boat', 'jeep', 'heli', 'piper')
# packages whose StaticMeshes are props too (exported as T3D by UCC: Gregaras' CC0 Battle Royale models)
STATIC_MESH_PACKAGES = ('battleRoyale',)


def convert(cfg: Config) -> Path:
    out_root = cfg.out_dir / 'Props'
    usage: dict[str, set[str]] = {}
    meshes: dict[str, dict] = {}
    for pkg_name in PACKAGES:
        try:
            pkg = Package(str(_package_path(cfg, pkg_name)))
        except FileNotFoundError:
            print(f'  ! {pkg_name}: not installed')
            continue
        tex_info = texture_index(cfg, pkg_name)
        n = 0
        for exp in pkg.exports:
            cls = pkg.class_of(exp)
            if cls == 'DeviceMesh':
                mesh = devmesh.read(pkg, exp.name)
                tris = [(_qualify(pkg_name, sec.material, tex_info) if sec.material else None, [mesh.vertices[i] for i in idx])
                        for sec, idx in mesh.triangles()] if mesh else None
            elif cls in ('LodMesh', 'Mesh'):   # Mesh: plain vertex mesh (#exec MESH IMPORT in mods)
                mesh = lodmesh.read(pkg, exp.name)
                tris = []
                for tex_index, _flags, corners in (mesh.triangles if mesh else []):
                    ref = mesh.textures[tex_index] if 0 <= tex_index < len(mesh.textures) else None
                    # no normals in a LodMesh: flat ones from the face (UE recomputes smooth ones)
                    p = [c[0] for c in corners]
                    nrm = _cross(_sub(p[1], p[0]), _sub(p[2], p[0]))
                    ln = math.sqrt(_dot(nrm, nrm)) or 1.0
                    nrm = tuple(c / ln for c in nrm)
                    tris.append((_qualify(pkg_name, ref, tex_info) if ref else None, [(c[0], nrm, c[1]) for c in corners]))
            else:
                continue
            if not tris:
                print(f'  ! {pkg_name}.{exp.name}: unreadable {cls}')
                continue
            mb = MeshBuilder(exp.name)
            mb.collision = 'complex' if cls == 'DeviceMesh' else 'none'
            area = 0.0
            for tex, vs in tris:
                tex = tex or 'Engine.DefaultTexture'
                info = tex_info.get(tex.lower(), {})
                flags = PF_MASKED if info.get('masked') else 0
                if exp.name.lower().startswith('heliglass'):
                    flags |= PF_TRANSLUCENT   # heliGlass Style=STY_Translucent
                key = material_key(tex, flags | info.get('polyflags', 0))
                usage.setdefault(tex.lower(), set()).add(key.split('|')[1].split('_')[0])
                p = [v[0] for v in vs]
                verts = [((v[0][0] * UU_TO_CM, v[0][1] * UU_TO_CM, v[0][2] * UU_TO_CM), v[1], v[2]) for v in vs]
                geo_n = _cross(_sub(p[1], p[0]), _sub(p[2], p[0]))
                avg_n = tuple(sum(v[1][i] for v in vs) for i in range(3))
                if _dot(geo_n, avg_n) < 0:
                    verts = [verts[0], verts[2], verts[1]]
                mb.add_triangle(key, *verts)
                area += _area(p)
            mb.lightmap_resolution = int(min(256, max(32, _next_pow2(math.sqrt(area) / 16.0))))
            mb.write(out_root / pkg_name)
            meshes[f'{pkg_name}/{exp.name}'] = {'mesh': f'{pkg_name}/{exp.name}', 'triangles': mb.triangle_count}
            n += 1
        if pkg_name in STATIC_MESH_PACKAGES:
            n += _static_meshes(cfg, pkg_name, tex_info, out_root, usage, meshes)
        print(f'[props] {pkg_name}: {n} meshes')
    path = out_root / 'props.json'
    path.write_text(json.dumps({'meshes': meshes, 'texture_usage': {k: sorted(v) for k, v in sorted(usage.items())}}, indent=1))
    return path


def _qualify(pkg_name: str, ref: str, tex_info: dict) -> str:
    """A mesh's texture reference: "Skins.WheelLTex" (this package) or "Rage.Jhumvee3" (a texture
    a mod reuses from another package)."""
    own = f'{pkg_name}.{ref}'
    if own.lower() in tex_info or '.' not in ref:
        return own
    return ref


def _static_meshes(cfg: Config, pkg_name: str, tex_info: dict, out_root: Path,
                   usage: dict[str, set[str]], meshes: dict[str, dict]) -> int:
    from . import t3d, ucc
    raw = cfg.work_dir / 'raw' / 'StaticMeshes' / pkg_name
    if not list(raw.glob('*.t3d')):
        ucc.batch_export(cfg, _package_path(cfg, pkg_name).relative_to(cfg.game_dir).as_posix(),
                         'StaticMesh', 't3d', raw)
    n = 0
    for f in sorted(raw.glob('*.t3d')):
        for blk in t3d.parse(t3d.read_text(f)).find('StaticMesh'):
            mb = MeshBuilder(blk.name)
            mb.collision = 'complex'
            area = 0.0
            for tri in t3d.static_mesh_triangles(blk):
                tex = tri.texture or 'Engine.DefaultTexture'
                info = tex_info.get(tex.lower(), {})
                key = material_key(tex, tri.poly_flags | (PF_MASKED if info.get('masked') else 0) | info.get('polyflags', 0))
                usage.setdefault(tex.lower(), set()).add(key.split('|')[1].split('_')[0])
                p = [v[0] for v in tri.verts]
                verts = [((v[0][0] * UU_TO_CM, v[0][1] * UU_TO_CM, v[0][2] * UU_TO_CM), v[1], v[2]) for v in tri.verts]
                geo_n = _cross(_sub(p[1], p[0]), _sub(p[2], p[0]))
                avg_n = tuple(sum(v[1][i] for v in tri.verts) for i in range(3))
                if _dot(avg_n, avg_n) > 1e-12 and _dot(geo_n, avg_n) < 0:
                    verts = [verts[0], verts[2], verts[1]]
                mb.add_triangle(key, *verts)
                area += _area(p)
            mb.lightmap_resolution = int(min(256, max(32, _next_pow2(math.sqrt(area) / 16.0))))
            mb.write(out_root / pkg_name)
            meshes[f'{pkg_name}/{blk.name}'] = {'mesh': f'{pkg_name}/{blk.name}', 'triangles': mb.triangle_count}
            n += 1
    return n
