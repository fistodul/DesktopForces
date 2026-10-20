"""Gameplay props from the game packages (Rage `DeviceMesh`es): vehicles and their wheels,
loadout crates, safes, flag bases, detonation locks, holdout timers.

    mfport.py props   ->  work/out/Props/<Package>/<Mesh>.mfmesh.* + Props/props.json

`import --steps props` builds them as /Game/MFImported/Props/<Package>/<Mesh>; game actors
use them when present (a converted install) and keep their placeholder shapes otherwise.
"""
from __future__ import annotations

import json
import math
from pathlib import Path

from . import devmesh
from .config import Config
from .level import PF_MASKED, UU_TO_CM, material_key, _cross, _dot, _sub, _area, _next_pow2
from .mfmesh import MeshBuilder
from .textures import _package_path, texture_index
from .upkg import Package

PACKAGES = ('Rage', 'RageGame')


def convert(cfg: Config) -> Path:
    out_root = cfg.out_dir / 'Props'
    usage: dict[str, set[str]] = {}
    meshes: dict[str, dict] = {}
    for pkg_name in PACKAGES:
        pkg = Package(str(_package_path(cfg, pkg_name)))
        tex_info = texture_index(cfg, pkg_name)
        n = 0
        for exp in pkg.exports:
            if pkg.class_of(exp) != 'DeviceMesh':
                continue
            mesh = devmesh.read(pkg, exp.name)
            if mesh is None:
                print(f'  ! {pkg_name}.{exp.name}: unreadable DeviceMesh')
                continue
            mb = MeshBuilder(exp.name)
            mb.collision = 'complex'
            area = 0.0
            for sec, (a, b, c) in mesh.triangles():
                tex = f'{pkg_name}.{sec.material}' if sec.material else 'Engine.DefaultTexture'
                info = tex_info.get(tex.lower(), {})
                flags = PF_MASKED if info.get('masked') else 0
                key = material_key(tex, flags | info.get('polyflags', 0))
                usage.setdefault(tex.lower(), set()).add(key.split('|')[1].split('_')[0])
                vs = [mesh.vertices[i] for i in (a, b, c)]
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
        print(f'[props] {pkg_name}: {n} meshes')
    path = out_root / 'props.json'
    path.write_text(json.dumps({'meshes': meshes, 'texture_usage': {k: sorted(v) for k, v in sorted(usage.items())}}, indent=1))
    return path
