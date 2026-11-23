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
from .level import PF_MASKED, PF_TRANSLUCENT, PF_TWOSIDED, UU_TO_CM, material_key, _cross, _dot, _sub, _area, _next_pow2
from .mfmesh import MeshBuilder
from .textures import _package_path, texture_index
from .upkg import Package

# + MF-mods packages with weapon models (missing ones are skipped)
PACKAGES = ('Rage', 'RageGame', 'RageWeapons', 'RageEffects', 'deagle', 'grenLaunch', 'battleRoyale', 'boat', 'jeep', 'heli', 'piper',
            # the servers' weapon mods (unverified pack)
            'AK74', 'm4a1', 'F2000', 'MAC10', 'M24', 'M18', 'm87', 'desert', 'famas', 'g36', 'glock', 'ultimax',
            'wa2000', 'improvedRL', 'parachute', 'BoxPhysics', 'mineGenerator',
            # MF-mods items with their own models
            'SeekerMines')
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
                tris = [(_qualify(pkg_name, sec.material, tex_info) if sec.material else None, [mesh.vertices[i] for i in idx], 0)
                        for sec, idx in mesh.triangles()] if mesh else None
            elif cls in ('LodMesh', 'Mesh'):   # Mesh: plain vertex mesh (#exec MESH IMPORT in mods)
                mesh = lodmesh.read(pkg, exp.name)
                tris = _lod_triangles(mesh, pkg_name, tex_info)
            else:
                continue
            if not tris:
                print(f'  ! {pkg_name}.{exp.name}: unreadable {cls}')
                continue
            # vertex animation that moves (Rage's: RageRingex's water ring, MachineGunMuzzleFlashMesh,
            # RTG_FlagMesh's wave; the community meshes repeat one pose): every frame its own mesh,
            # <name>_F<n>, the game swaps them (UDFMeshFlipbook)
            frames = []
            if cls == 'LodMesh' and mesh.anim_frames > 1:
                first = mesh.frame_positions(0)
                moving = any(max((abs(a - b) for p0, p1 in zip(first, mesh.frame_positions(f)) for a, b in zip(p0, p1)), default=0.0) > 0.01
                             for f in range(1, mesh.anim_frames))
                if moving:
                    frames = [(f'{exp.name}_F{f}', _lod_triangles(lodmesh.read(pkg, exp.name, f), pkg_name, tex_info))
                              for f in range(1, mesh.anim_frames)]
            for name, mesh_tris in [(exp.name, tris)] + frames:
                count = _write_mesh(name, mesh_tris, cls, tex_info, usage, out_root / pkg_name)
                meshes[f'{pkg_name}/{name}'] = {'mesh': f'{pkg_name}/{name}', 'triangles': count}
                n += 1
            if frames:
                meshes[f'{pkg_name}/{exp.name}'].update(
                    frames=mesh.anim_frames, sequences=[{'name': q[0], 'first': q[1], 'count': q[2], 'rate': q[3]} for q in mesh.sequences])
        if pkg_name in STATIC_MESH_PACKAGES:
            n += _static_meshes(cfg, pkg_name, tex_info, out_root, usage, meshes)
        print(f'[props] {pkg_name}: {n} meshes')
    path = out_root / 'props.json'
    path.write_text(json.dumps({'meshes': meshes, 'texture_usage': {k: sorted(v) for k, v in sorted(usage.items())}}, indent=1))
    return path


def _lod_triangles(mesh, pkg_name: str, tex_info: dict) -> list:
    """A LodMesh frame's triangles: (texture key, 3 x (pos uu, flat normal, uv), face flags)."""
    tris = []
    for tex_index, face_flags, corners in (mesh.triangles if mesh else []):
        ref = mesh.textures[tex_index] if 0 <= tex_index < len(mesh.textures) else None
        # no normals in a LodMesh: flat ones from the face (UE recomputes smooth ones)
        p = [c[0] for c in corners]
        nrm = _cross(_sub(p[1], p[0]), _sub(p[2], p[0]))
        ln = math.sqrt(_dot(nrm, nrm)) or 1.0
        nrm = tuple(c / ln for c in nrm)
        tris.append((_qualify(pkg_name, ref, tex_info) if ref else None, [(c[0], nrm, c[1]) for c in corners], face_flags))
    return tris


def _write_mesh(name: str, tris: list, cls: str, tex_info: dict, usage: dict[str, set[str]], out_dir: Path) -> int:
    """One prop mesh file from (texture key, corners, face flags) triangles; its triangle count."""
    mb = MeshBuilder(name)
    mb.collision = 'complex' if cls == 'DeviceMesh' else 'none'
    area = 0.0
    for tex, vs, face_flags in tris:
        tex = tex or 'Engine.DefaultTexture'
        info = tex_info.get(tex.lower(), {})
        # UE1 alpha-tests a mesh face only when its own flags (LodMesh Materials) or the
        # texture's PolyFlags say PF_Masked; bMasked alone just means the palette has a
        # see-through index 0 (tyres, car frags: MF drew them solid, their S3TC copy too).
        # PF_TwoSided faces aren't culled from behind (parachute canopy, flag cloth)
        flags = face_flags & (PF_MASKED | PF_TRANSLUCENT | PF_TWOSIDED)
        if name.lower().startswith('heliglass'):
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
    mb.write(out_dir)
    return mb.triangle_count


def _qualify(pkg_name: str, ref: str, tex_info: dict) -> str:
    """A mesh's texture reference: "Skins.WheelLTex" (this package) or "Rage.Jhumvee3" (a texture
    a mod reuses from another package)."""
    own = f'{pkg_name}.{ref}'
    if own.lower() in tex_info or '.' not in ref:
        return own
    return ref


def _static_meshes(cfg: Config, pkg_name: str, tex_info: dict, out_root: Path,
                   usage: dict[str, set[str]], meshes: dict[str, dict]) -> int:
    from . import staticmesh, t3d, ucc
    raw = cfg.work_dir / 'raw' / 'StaticMeshes' / pkg_name
    pkg = Package(str(_package_path(cfg, pkg_name)))
    if not list(raw.glob('*.t3d')):
        ucc.batch_export(cfg, _package_path(cfg, pkg_name).relative_to(cfg.game_dir).as_posix(),
                         'StaticMesh', 't3d', raw)
    n = 0
    for f in sorted(raw.glob('*.t3d')):
        for blk in t3d.parse(t3d.read_text(f)).find('StaticMesh'):
            mb = MeshBuilder(blk.name)
            mb.collision = 'complex'
            area = 0.0
            tris = t3d.static_mesh_triangles(blk)
            # their sections' textures, as drawn (mflib/staticmesh.py)
            drawn = staticmesh.triangle_textures([t.texture for t in tris], staticmesh.sections(pkg, blk.name))
            for tri, tex in zip(tris, drawn or ()):
                tri.texture = tex
            for tri in t3d.orient_triangles(tris):
                tex = tri.texture or 'Engine.DefaultTexture'
                info = tex_info.get(tex.lower(), {})
                key = material_key(tex, tri.poly_flags | (PF_MASKED if info.get('masked') else 0) | info.get('polyflags', 0))
                usage.setdefault(tex.lower(), set()).add(key.split('|')[1].split('_')[0])
                p = [v[0] for v in tri.verts]
                verts = [((v[0][0] * UU_TO_CM, v[0][1] * UU_TO_CM, v[0][2] * UU_TO_CM), v[1], v[2]) for v in tri.verts]
                mb.add_triangle(key, *verts)
                area += _area(p)
            mb.lightmap_resolution = int(min(256, max(32, _next_pow2(math.sqrt(area) / 16.0))))
            mb.write(out_root / pkg_name)
            meshes[f'{pkg_name}/{blk.name}'] = {'mesh': f'{pkg_name}/{blk.name}', 'triangles': mb.triangle_count}
            n += 1
    return n
