"""Merge two aligned converted maps area by area (Docs/WarehouseMerge.md).

Input: `merge/<map>.toml` - which version ("MF" = base, "MFS" = remake) each zone of the
base map comes from. Zones are the base map's zone bounding boxes; they overlap, so a
point belongs to the smallest box containing it (outside all boxes: `default`).

World geometry is clipped against the boxes, so a polygon crossing a border is cut there
and each side kept from its own version. Props and lights go by their position. Gameplay
actors (spawns, crates, objectives, sounds) always come from the base map so every game
mode keeps working; the remake contributes geometry, props, lights and its extras
(MFS:* legacy records such as zombie / buggy spots).

Output: work/out/Maps/<name>/ (level.json + .mfmesh), importable like any converted map.
"""
from __future__ import annotations

import json
import math
import shutil
import tomllib
from pathlib import Path

from .config import Config, TOOL_DIR
from .mfmesh import MeshBuilder, read

EPS = 0.5   # cm; polygons lying on a zone face count as inside (BSP walls bound their zone)


# --- polygon clipping ------------------------------------------------------------------

def _lerp(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def _vlerp(v0, v1, t):
    return (_lerp(v0[0], v1[0], t), _lerp(v0[1], v1[1], t), _lerp(v0[2], v1[2], t))


def split(poly, axis, value, world_offset):
    """Split a convex polygon of (pos, normal, uv) by the plane pos[axis] = value.
    Returns (below, above); vertices on the plane go to both."""
    below, above = [], []
    n = len(poly)
    d = [poly[i][0][axis] + world_offset[axis] - value for i in range(n)]
    for i in range(n):
        j = (i + 1) % n
        vi, di, dj = poly[i], d[i], d[j]
        if di <= EPS:
            below.append(vi)
        if di >= -EPS:
            above.append(vi)
        if (di < -EPS and dj > EPS) or (di > EPS and dj < -EPS):
            v = _vlerp(vi, poly[j], di / (di - dj))
            below.append(v)
            above.append(v)
    return (below if len(below) >= 3 else None), (above if len(above) >= 3 else None)


def classify(poly, regions, default, world_offset):
    """-> list of (polygon piece, version) covering `poly`."""
    out = []
    todo = [(poly, 0)]
    while todo:
        p, start = todo.pop()
        lo_p = [min(v[0][a] for v in p) + world_offset[a] for a in range(3)]
        hi_p = [max(v[0][a] for v in p) + world_offset[a] for a in range(3)]
        for r in range(start, len(regions)):
            _, lo, hi, version = regions[r]
            if any(hi_p[a] < lo[a] - EPS or lo_p[a] > hi[a] + EPS for a in range(3)):
                continue
            # peel off the parts outside this box one plane at a time; they continue with
            # the next (bigger) boxes, the rest is inside
            inside = p
            for a in range(3):
                if inside is None:
                    break
                below, above = split(inside, a, lo[a], world_offset)
                if below is not None and any(v[0][a] + world_offset[a] < lo[a] - EPS for v in below):
                    todo.append((below, r + 1))
                inside = above
                if inside is None:
                    break
                below, above = split(inside, a, hi[a], world_offset)
                if above is not None and any(v[0][a] + world_offset[a] > hi[a] + EPS for v in above):
                    todo.append((above, r + 1))
                inside = below
            if inside is not None:
                out.append((inside, version))
            break
        else:
            out.append((p, default))
    return out


# --- merge -----------------------------------------------------------------------------

FILL_DIST = 150.0   # cm from a border where the other version's floor may fill a hole
CELL = 50.0


def _fan(mb, key, piece):
    for i in range(1, len(piece) - 1):
        mb.add_triangle(key, piece[0], piece[i], piece[i + 1])


def _cells(poly, off):
    """Grid cells (CELL) covered by a floor polygon, with its height there (approximate:
    the polygon's bounding box cells whose centre is inside, z = mean height)."""
    xs = [v[0][0] + off[0] for v in poly]
    ys = [v[0][1] + off[1] for v in poly]
    z = sum(v[0][2] for v in poly) / len(poly) + off[2]
    n = len(poly)
    for i in range(math.floor(min(xs) / CELL), math.floor(max(xs) / CELL) + 1):
        for j in range(math.floor(min(ys) / CELL), math.floor(max(ys) / CELL) + 1):
            px, py = (i + 0.5) * CELL, (j + 0.5) * CELL
            inside = True
            sign = 0.0
            for k in range(n):
                ax, ay, bx, by = xs[k], ys[k], xs[(k + 1) % n], ys[(k + 1) % n]
                cr = (bx - ax) * (py - ay) - (by - ay) * (px - ax)
                if abs(cr) > 1e-6:
                    if sign == 0.0:
                        sign = cr
                    elif cr * sign < 0:
                        inside = False
                        break
            if inside:
                yield (i, j), z


def _vol(lo, hi):
    return (hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2])


class Merger:
    def __init__(self, cfg: Config, preset: str):
        self.cfg = cfg
        spec = tomllib.loads((TOOL_DIR / 'merge' / f'{preset}.toml').read_text())
        self.spec = spec
        m = spec['map']
        self.name, self.default = m['name'], m['default']
        self.src = {'MF': cfg.out_dir / 'Maps' / m['base'], 'MFS': cfg.out_dir / 'Maps' / m['remake']}
        self.lvl = {k: json.loads((p / 'level.json').read_text()) for k, p in self.src.items()}
        self.out = cfg.out_dir / 'Maps' / self.name
        base = self.lvl['MF']
        # Every base zone is a region. Zones nobody picked (small ones like captain zones)
        # inherit from the nearest picked zone. Boxes grow by `margin` so the inside of walls
        # between two zones (no zone's polygons reach in there) belongs to a neighbour, not
        # to `default` - which is only for space outside the original map.
        margin = float(m.get('margin', 100.0))
        boxes = []
        for z, (lo, hi) in base['zone_bounds'].items():
            zname = base['zones'][int(z)] or f'zone{z}'
            if zname.startswith('SkyZone'):
                continue
            boxes.append((zname, lo, hi))

        def gap(b1, b2):
            return math.sqrt(sum(max(0.0, b1[1][a] - b2[2][a], b2[1][a] - b1[2][a]) ** 2 for a in range(3)))
        picked = [b for b in boxes if b[0] in spec['zones']]
        regions = []
        for zname, lo, hi in boxes:
            version = spec['zones'].get(zname)
            if version is None and picked:
                near = min(picked, key=lambda b: (gap((zname, lo, hi), b), _vol(b[1], b[2])))
                version = spec['zones'][near[0]]
            regions.append((_vol(lo, hi), zname, lo, hi, version or self.default))
        regions.sort(key=lambda r: r[0])
        # real boxes first (smallest wins); the grown boxes only claim space no zone has,
        # so a margin never eats into a neighbouring zone
        self.regions = [(n, lo, hi, v) for _, n, lo, hi, v in regions]
        self.regions += [(n + '+margin', [x - margin for x in lo], [x + margin for x in hi], v)
                         for _, n, lo, hi, v in regions]

    def owner(self, p) -> str:
        for _, lo, hi, version in self.regions:
            if all(lo[a] - EPS <= p[a] <= hi[a] + EPS for a in range(3)):
                return version
        return self.default

    def keep(self, version_of_source: str, p) -> bool:
        v = self.owner(p)
        return v == version_of_source or v == 'both'

    def run(self) -> Path:
        if self.out.exists():
            shutil.rmtree(self.out)
        (self.out / 'geo').mkdir(parents=True)
        (self.out / 'meshes').mkdir()
        world, stats = [], {'MF': 0, 'MFS': 0}
        field = None
        if self.spec['map'].get('warp', False):
            from .warp import estimate
            field = estimate(self.src['MF'], self.src['MFS'], self.out / 'warp.json')
            print(f'  warp: {len(field.p)} control points')

        def moved(ver, p):
            if ver != 'MFS' or field is None:
                return p
            dx, dy = field(p[0], p[1])
            return (p[0] + dx, p[1] + dy, p[2])

        # pass 1: clip every chunk; keep the owner's pieces, remember the rest
        builders, rejected = [], []      # rejected: (builder index, material key, piece, world offset)
        for ver, root in self.src.items():
            for w in self.lvl[ver]['world']:
                src = read(root / f"{w['mesh']}.mfmesh.json")
                mb = MeshBuilder(f'{ver}_{src.name}', materials=list(src.materials), collision=src.collision,
                                 lightmap_resolution=src.lightmap_resolution, render=src.render)
                off = w['location']
                bi = len(builders)
                builders.append((ver, w, mb))
                for mat, tris in src.tris.items():
                    for tri in tris:
                        if ver == 'MFS' and field is not None:
                            tri = [(tuple(a - o for a, o in zip(moved(ver, tuple(c + o for c, o in zip(v[0], off))), off)), v[1], v[2])
                                   for v in tri]
                        for piece, v in classify(list(tri), self.regions, self.default, off):
                            if v == ver or v == 'both':
                                _fan(mb, src.materials[mat], piece)
                            elif piece[0][1][2] > 0.7:
                                rejected.append((bi, src.materials[mat], piece, off))

        # pass 2: fill holes at borders - a floor the other version had within FILL_DIST of a
        # border comes back where the merged map has no floor at that height
        cover = {}
        for ver, w, mb in builders:
            if mb.render:
                continue
            off = w['location']
            for tris in mb.tris.values():
                for tri in tris:
                    if tri[0][1][2] > 0.7:
                        for cell, z in _cells(tri, off):
                            cover.setdefault(cell, []).append(z)
        filled = 0
        for bi, key, piece, off in rejected:
            c = tuple(sum(v[0][a] for v in piece) / len(piece) + off[a] for a in range(3))
            own = self.owner((c[0], c[1], c[2] + 50.0))
            near_border = any(self.owner((c[0] + dx, c[1] + dy, c[2] + 50.0)) != own
                              for dx, dy in ((FILL_DIST, 0), (-FILL_DIST, 0), (0, FILL_DIST), (0, -FILL_DIST)))
            if not near_border:
                continue
            cells = list(_cells(piece, off))
            if cells and all(not any(abs(z - fz) < 60.0 for fz in cover.get(cell, ())) for cell, z in cells):
                _fan(builders[bi][2], key, piece)
                filled += 1

        for ver, w, mb in builders:
            if mb.triangle_count:
                mb.write(self.out / 'geo')
                world.append(dict(w, mesh=f'geo/{mb.name}', triangles=mb.triangle_count))
                stats[ver] += mb.triangle_count
        print(f'  border fill: {filled} floor pieces')

        spawns, meshes = [], set()
        for ver in ('MF', 'MFS'):
            for s in self.lvl[ver]['spawns']:
                t = s['type']
                if ver == 'MFS':
                    s = dict(s, location=list(moved(ver, s['location'])))
                if t in ('static_mesh', 'point_light', 'spot_light'):
                    if not self.keep(ver, s['location']):
                        continue
                    if t == 'static_mesh':
                        new = f"meshes/{ver.lower()}_{Path(s['mesh']).name}"
                        if new not in meshes:
                            mb = read(self.src[ver] / f"{s['mesh']}.mfmesh.json")
                            mb.name = Path(new).name
                            mb.write(self.out / 'meshes')
                            meshes.add(new)
                        s = dict(s, mesh=new)
                elif ver == 'MFS' and not (t == 'legacy' and s.get('mf_class', '').startswith('MFS:')):
                    continue    # gameplay actors come from the original map
                spawns.append(s)

        usage = {}
        for ver in ('MF', 'MFS'):
            for k, v in self.lvl[ver].get('texture_usage', {}).items():
                usage.setdefault(k, set()).update(v)
        base = self.lvl['MF']
        data = dict(base, map=self.name, world=world, spawns=spawns, actors=[],
                    texture_usage={k: sorted(v) for k, v in sorted(usage.items())},
                    merge={'preset': self.spec['map'], 'zones': self.spec['zones']})
        (self.out / 'level.json').write_text(json.dumps(data, indent=1))
        print(f'[{self.name}] triangles MF {stats["MF"]} + MFS {stats["MFS"]}, {len(spawns)} spawns, '
              f'{len(meshes)} prop meshes')
        return self.out / 'level.json'
