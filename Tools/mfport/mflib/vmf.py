"""Convert a Valve Hammer map (.vmf) - the Mobile Forces Source maps - into mfport's
intermediate format (.mfmesh chunks + level.json), so the same UE importer builds it.

Used for the mf-warehouse merge (Docs/WarehouseMerge.md): the MFS map is brought in as
its own level first, aligned to the original, then merged area by area.

- Brushes ("solid") are convex: each side is a plane given by three points; the face
  polygon is a big quad on that plane clipped by every other plane of the solid.
- UVs use Valve's axes: u = (P . uaxis / uscale + uoffset) / texture width.
- Tool textures: NODRAW / SKIP / HINT / AREAPORTAL / OCCLUDER faces are dropped,
  CLIP / PLAYERCLIP / INVISIBLE(LADDER) become collision-only, SKYBOX faces become sky.
- Materials map to MF texture keys through `vmf_materials.toml` (HL2 materials must
  never be used as-is); unmapped ones fall back to the default texture and are listed.
- Units: Hammer units * `scale` (default 1.0 = treat 1 Hammer unit as 1 MF uu) then the
  usual UU_TO_CM. The merge step decides the real scale/offset.
"""
from __future__ import annotations

import json
import math
import re
import struct
import tomllib
from collections import Counter
from pathlib import Path

from .config import Config, TOOL_DIR, UU_TO_CM
from .mfmesh import MeshBuilder
from .sourcemdl import read_model

# Prop models we may use: MF meshes recompiled for Source by the MFS team. Valve/HL2
# models (props_c17, props_junk, ...) are skipped and listed for replacement.
PROP_MODEL_DIRS = ('models/warehouse/',)

DROP_TOOLS = ('TOOLS/TOOLSNODRAW', 'TOOLS/TOOLSSKIP', 'TOOLS/TOOLSHINT', 'TOOLS/TOOLSAREAPORTAL',
              'TOOLS/TOOLSOCCLUDER', 'TOOLS/TOOLSTRIGGER', 'TOOLS/TOOLSFOG', 'TOOLS/TOOLSLIGHT',
              'TOOLS/TOOLSORIGIN', 'TOOLS/TOOLSBLOCKLIGHT', 'TOOLS/TOOLSBLOCKBULLETS', 'TOOLS/TOOLSBLACK')
COLLISION_TOOLS = ('TOOLS/TOOLSCLIP', 'TOOLS/TOOLSPLAYERCLIP', 'TOOLS/TOOLSINVISIBLE', 'TOOLS/TOOLSINVISIBLELADDER',
                   'TOOLS/TOOLSNPCCLIP')
SKY_TOOLS = ('TOOLS/TOOLSSKYBOX', 'TOOLS/TOOLSSKYBOX2D')
CHUNK = 2048.0
EPS = 0.01

# --- KeyValues ------------------------------------------------------------------------

_TOKEN = re.compile(r'"([^"]*)"|(\{)|(\})|([^\s"{}]+)')


class KV:
    __slots__ = ('name', 'keys', 'children')

    def __init__(self, name: str):
        self.name = name
        self.keys: dict[str, str] = {}
        self.children: list[KV] = []

    def find(self, name: str) -> list['KV']:
        return [c for c in self.children if c.name == name]


def parse_kv(text: str) -> KV:
    root = KV('root')
    stack = [root]
    pending = None
    for m in _TOKEN.finditer(text):
        s, open_, close, bare = m.groups()
        if open_:
            blk = KV(pending or '')
            stack[-1].children.append(blk)
            stack.append(blk)
            pending = None
        elif close:
            if len(stack) > 1:
                stack.pop()
            pending = None
        else:
            tok = s if s is not None else bare
            if pending is None:
                pending = tok
            else:
                stack[-1].keys.setdefault(pending, tok)
                pending = None
    return root


# --- geometry -------------------------------------------------------------------------

def _v(s: str) -> tuple[float, float, float]:
    return tuple(float(x) for x in s.split())


def _sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def _add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def _mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def _dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def _cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _norm(a):
    l = math.sqrt(_dot(a, a))
    return (a[0] / l, a[1] / l, a[2] / l) if l > 1e-12 else (0.0, 0.0, 0.0)


def _plane(points) -> tuple[tuple, float]:
    p0, p1, p2 = points
    n = _norm(_cross(_sub(p2, p0), _sub(p1, p0)))
    return n, _dot(n, p0)


def _base_poly(n, d, size=65536.0):
    # any vector not parallel to n
    up = (0.0, 0.0, 1.0) if abs(n[2]) < 0.9 else (1.0, 0.0, 0.0)
    u = _norm(_cross(up, n))
    v = _cross(n, u)
    c = _mul(n, d)
    return [_add(c, _add(_mul(u, -size), _mul(v, -size))), _add(c, _add(_mul(u, size), _mul(v, -size))),
            _add(c, _add(_mul(u, size), _mul(v, size))), _add(c, _add(_mul(u, -size), _mul(v, size)))]


def _clip(poly, n, d):
    """Keep the part of poly behind plane (n, d) (dot(n, p) <= d)."""
    out = []
    for i, a in enumerate(poly):
        b = poly[(i + 1) % len(poly)]
        da, db = _dot(n, a) - d, _dot(n, b) - d
        if da <= EPS:
            out.append(a)
        if (da < -EPS and db > EPS) or (da > EPS and db < -EPS):
            t = da / (da - db)
            out.append(_add(a, _mul(_sub(b, a), t)))
    return out


def solid_faces(solid: KV):
    sides = solid.find('side')
    planes = []
    for s in sides:
        pts = re.findall(r'\(([^)]*)\)', s.keys.get('plane', ''))
        if len(pts) != 3:
            return []
        planes.append(_plane([_v(p) for p in pts]))
    # orient outward: centre of all plane points is inside a convex solid
    centre = (0.0, 0.0, 0.0)
    allpts = [_v(p) for s in sides for p in re.findall(r'\(([^)]*)\)', s.keys['plane'])]
    for p in allpts:
        centre = _add(centre, p)
    centre = _mul(centre, 1.0 / len(allpts))
    planes = [(n, d) if _dot(n, centre) - d < 0 else (_mul(n, -1), -d) for n, d in planes]
    faces = []
    for i, (n, d) in enumerate(planes):
        poly = _base_poly(n, d)
        for j, (n2, d2) in enumerate(planes):
            if i != j:
                poly = _clip(poly, n2, d2)
                if len(poly) < 3:
                    break
        # unbounded leftovers of degenerate solids (Hammer's world is +-16384)
        if len(poly) >= 3 and all(abs(c) <= 32768 for p in poly for c in p):
            faces.append((sides[i], n, poly))
    return faces


def _axis(s: str):
    m = re.match(r'\[\s*([-\d.e]+)\s+([-\d.e]+)\s+([-\d.e]+)\s+([-\d.e]+)\s*\]\s*([-\d.e]+)', s or '')
    if not m:
        return (1.0, 0.0, 0.0), 0.0, 0.25
    x, y, z, off, scale = map(float, m.groups())
    return (x, y, z), off, scale if scale != 0 else 0.25


# --- material mapping -----------------------------------------------------------------

def load_material_map() -> dict[str, str]:
    f = TOOL_DIR / 'vmf_materials.toml'
    if not f.exists():
        return {}
    data = tomllib.loads(f.read_text())
    return {k.upper(): v for k, v in data.get('materials', {}).items()}


class VmfConverter:
    def __init__(self, cfg: Config, vmf_path: Path, name: str, scale: float = 1.0,
                 offset=(0.0, 0.0, 0.0), yaw: float = 0.0, zscale: float | None = None,
                 prop_scale: float | None = None):
        self.cfg = cfg
        self.path = vmf_path
        self.name = name
        self.scale = scale
        self.zscale = scale if zscale is None else zscale          # remakes may squash height differently
        self.prop_scale = scale if prop_scale is None else prop_scale
        self.offset = offset
        self.yaw = math.radians(yaw)
        self.out = cfg.out_dir / 'Maps' / name
        self.matmap = load_material_map()
        self.texture_index: dict[str, dict] = {}
        self.unmapped: Counter = Counter()
        self.usage: dict[str, set] = {}
        self.models: dict[str, str | None] = {}
        self.skipped_models: Counter = Counter()

    # MF texture lookup by bare name (WAREHOUSE/WARE_METAL_34 -> rage_warehouse.metal.ware_metal_34)
    def _mf_textures(self):
        if self.texture_index:
            return self.texture_index
        from .textures import texture_index
        for pkg in ('rage_warehouse', 'rage_generic'):
            for k, info in texture_index(self.cfg, pkg).items():
                self.texture_index.setdefault(info['name'].lower(), info)
        return self.texture_index

    def material_key(self, material: str) -> tuple[str | None, tuple[int, int]]:
        m = material.upper()
        target = self.matmap.get(m)
        if target is None:
            info = self._mf_textures().get(m.split('/')[-1].lower())
            target = info['key'] if info else None
        if target is None:
            self.unmapped[m] += 1
            return None, (512, 512)
        info = self._mf_textures().get(target.split('.')[-1].lower())
        size = (info['usize'], info['vsize']) if info else (256, 256)
        return target, size

    def xform(self, p):
        c, s = math.cos(self.yaw), math.sin(self.yaw)
        x, y, z = p[0] * self.scale, p[1] * self.scale, p[2] * self.zscale
        x, y = x * c - y * s, x * s + y * c
        # Hammer is right-handed (Y left); Unreal is left-handed (Y right): mirror Y
        return ((x + self.offset[0]) * UU_TO_CM, (-y + self.offset[1]) * UU_TO_CM, (z + self.offset[2]) * UU_TO_CM)

    def run(self) -> Path:
        root = parse_kv(self.path.read_text(encoding='latin-1', errors='replace'))
        world = root.find('world')[0]
        solids = list(world.find('solid'))
        for ent in root.find('entity'):
            if ent.keys.get('classname') in ('func_detail', 'func_brush', 'func_wall', 'func_illusionary'):
                solids += ent.find('solid')
        chunks: dict[tuple, MeshBuilder] = {}
        coll: dict[tuple, MeshBuilder] = {}
        nfaces = 0
        for solid in solids:
            for side, n, poly in solid_faces(solid):
                mat = side.keys.get('material', '').upper()
                if mat in DROP_TOOLS or mat.startswith('TOOLS/TOOLSNODRAW'):
                    continue
                render = mat not in COLLISION_TOOLS
                key = None
                if mat in SKY_TOOLS:
                    key, size, mode = 'rage_warehouse.sky.sky_00|sky', (256, 256), 'sky'
                elif render:
                    tex, size = self.material_key(mat)
                    key = f'{tex or "Engine.DefaultTexture"}|opaque'
                centre = tuple(sum(p[i] for p in poly) / len(poly) for i in range(3))
                wc = self.xform(centre)
                cell = (math.floor(wc[0] / (CHUNK * UU_TO_CM)), math.floor(wc[1] / (CHUNK * UU_TO_CM)))
                pivot = ((cell[0] + 0.5) * CHUNK * UU_TO_CM, (cell[1] + 0.5) * CHUNK * UU_TO_CM, 0.0)
                verts = [self.xform(p) for p in poly]
                # mirroring flips handedness: reverse order to keep faces outward
                verts = list(reversed(verts))
                nn = _norm(_sub(self.xform(_add(centre, n)), wc))
                if render:
                    ua, uo, us = _axis(side.keys.get('uaxis'))
                    va, vo, vs = _axis(side.keys.get('vaxis'))

                    def uv(p):
                        return ((_dot(p, ua) / us + uo) / size[0], (_dot(p, va) / vs + vo) / size[1])
                    uvs = list(reversed([uv(p) for p in poly]))
                    mb = chunks.setdefault(cell, MeshBuilder(f'World_{cell[0]}_{cell[1]}'.replace('-', 'm'), collision='none'))
                    tex = key.split('|')[0]
                    self.usage.setdefault(tex.lower(), set()).add(key.split('|')[1])
                    vs_ = [(_sub(v, pivot), nn, t) for v, t in zip(verts, uvs)]
                    self._fan(mb, key, vs_, nn)
                cb = coll.setdefault(cell, MeshBuilder(f'Collision_{cell[0]}_{cell[1]}'.replace('-', 'm'), render=False))
                self._fan(cb, 'Engine.DefaultTexture|collision', [(_sub(v, pivot), nn, (0, 0)) for v in verts], nn)
                nfaces += 1
        world_out = []
        for cell, mb in list(chunks.items()) + list(coll.items()):
            mb.write(self.out / 'geo')
            pivot = ((cell[0] + 0.5) * CHUNK * UU_TO_CM, (cell[1] + 0.5) * CHUNK * UU_TO_CM, 0.0)
            world_out.append({'mesh': f'geo/{mb.name}', 'location': pivot, 'render': mb.render,
                              'triangles': mb.triangle_count, 'lightmap_resolution': 256})
        spawns = self.entities(root)
        data = {'map': self.name, 'source': str(self.path), 'uu_to_cm': UU_TO_CM, 'vmf_scale': self.scale,
                'world': world_out, 'static_meshes': {}, 'spawns': spawns, 'actors': [],
                'lightmass': {'environment_color': [0.3, 0.3, 0.32], 'environment_intensity': 0.4},
                'texture_usage': {k: sorted(v) for k, v in self.usage.items()},
                'unmapped_materials': dict(self.unmapped.most_common()),
                'skipped_models': dict(self.skipped_models.most_common())}
        path = self.out / 'level.json'
        path.write_text(json.dumps(data, indent=1))
        print(f'[{self.name}] {len(solids)} solids, {nfaces} faces, {len(chunks)} chunks, '
              f'{len(spawns)} spawns, {len(self.unmapped)} unmapped materials ({sum(self.unmapped.values())} faces), '
              f'{sum(1 for m in self.models.values() if m)} props converted, '
              f'{sum(self.skipped_models.values())} Valve props skipped')
        return path

    def convert_model(self, model: str) -> str | None:
        """Source prop model -> meshes/<name>.mfmesh (Source units as MF uu, Y mirrored)."""
        key = model.lower()
        if key in self.models:
            return self.models[key]
        game = self.path.parents[1]                      # .../mobileforcessource/mapsrc/x.vmf
        mdl = game / model
        if not mdl.exists():
            mdl = next((p for p in mdl.parent.glob('*') if p.name.lower() == mdl.name.lower()), mdl)
        try:
            sm = read_model(mdl)
        except (OSError, ValueError, struct.error) as e:
            print(f'  ! {model}: {e}')
            self.models[key] = None
            return None
        name = Path(key).stem
        mb = MeshBuilder(name)
        keys = []
        for tex in sm.textures:
            target, _ = self.material_key(f'{(sm.tex_dirs or [""])[0]}{tex}')
            keys.append(f'{target or "Engine.DefaultTexture"}|opaque')
            self.usage.setdefault((target or 'Engine.DefaultTexture').lower(), set()).add('opaque')
        for tex, tri in sm.triangles:
            vs = [((p[0] * UU_TO_CM, -p[1] * UU_TO_CM, p[2] * UU_TO_CM), (n[0], -n[1], n[2]), uv) for p, n, uv in tri]
            nrm = _norm(tuple(sum(v[1][i] for v in vs) for i in range(3)))
            self._fan(mb, keys[tex] if tex < len(keys) else 'Engine.DefaultTexture|opaque', vs, nrm)
        mb.lightmap_resolution = 64
        mb.write(self.out / 'meshes')
        self.models[key] = f'meshes/{name}'
        return self.models[key]

    @staticmethod
    def _fan(mb: MeshBuilder, key, vs, n):
        # orient so cross(v1-v0, v2-v0) follows the normal (mfmesh convention)
        if len(vs) >= 3 and _dot(_cross(_sub(vs[1][0], vs[0][0]), _sub(vs[2][0], vs[0][0])), n) < 0:
            vs = list(reversed(vs))
        for i in range(1, len(vs) - 1):
            mb.add_triangle(key, vs[0], vs[i], vs[i + 1])

    def entities(self, root: KV) -> list[dict]:
        out = []
        for ent in root.find('entity'):
            cls = ent.keys.get('classname', '')
            if 'origin' not in ent.keys:
                continue
            loc = self.xform(_v(ent.keys['origin']))
            pitch, yaw, roll = (_v(ent.keys['angles']) if 'angles' in ent.keys else (0.0, 0.0, 0.0))
            rot = {'pitch': -pitch, 'yaw': -(yaw + math.degrees(self.yaw)), 'roll': roll}
            base = {'name': f'{cls}_{ent.keys.get("id", "")}', 'location': loc, 'rotation': rot}
            if cls in ('info_player_deathmatch', 'info_player_start'):
                out.append(dict(base, type='actor', **{'class': '/Script/DesktopForces.DFPlayerStart'}, props={'TeamNumber': '255'}))
            elif cls in ('info_player_combine', 'info_player_captain_blue'):
                out.append(dict(base, type='actor', **{'class': '/Script/DesktopForces.DFPlayerStart'}, props={'TeamNumber': '0'}))
            elif cls in ('info_player_rebel', 'info_player_captain_red'):
                out.append(dict(base, type='actor', **{'class': '/Script/DesktopForces.DFPlayerStart'}, props={'TeamNumber': '1'}))
            elif cls in ('light', 'light_spot'):
                col = [float(x) for x in ent.keys.get('_light', '255 255 255 200').split()[:4]]
                while len(col) < 4:
                    col.append(200.0)
                rec = dict(base, color=[col[0] / 255, col[1] / 255, col[2] / 255], intensity=col[3] / 200 * 3.0,
                           radius=float(ent.keys.get('_distance', 0) or 0) * self.scale * UU_TO_CM or 1500.0)
                out.append(dict(rec, type='spot_light' if cls == 'light_spot' else 'point_light',
                                cone=float(ent.keys.get('_cone', 45))))
            elif cls.startswith('prop_') and not cls.startswith(('prop_vehicle', 'prop_holdout')) \
                    and ent.keys.get('model', '').lower().startswith(PROP_MODEL_DIRS):
                mesh = self.convert_model(ent.keys['model'])
                if mesh:
                    s = self.prop_scale * float(ent.keys.get('modelscale', 1) or 1)
                    out.append(dict(base, type='static_mesh', mesh=mesh, scale=[s, s, s],
                                    collision=ent.keys.get('solid', '6') != '0'))
            elif cls.startswith('prop_') and 'model' in ent.keys and not cls.startswith(('prop_vehicle', 'prop_holdout')):
                # HL2 / Valve model: not usable, listed for replacement with MF or new art
                self.skipped_models[ent.keys['model'].lower()] += 1
            elif cls.startswith(('npc_', 'prop_vehicle', 'prop_holdout', 'item_', 'weapon_')):
                # kept as data for the merge (zombie easter eggs, buggy spawns, holdout ...)
                out.append(dict(base, type='legacy', mf_class=f'MFS:{cls}', mf_name=base['name'], team='255',
                                props={k: v for k, v in ent.keys.items() if k not in ('origin', 'angles')}))
        return out
