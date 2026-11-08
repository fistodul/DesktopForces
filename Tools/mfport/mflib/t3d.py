"""Parse UE1 / Mobile Forces T3D text (as written by `ucc batchexport ... Level t3d` and `StaticMesh t3d`).

The result is a light tree of `Block`s (Begin X ... End X) whose `props` dict maps
property names to *raw text values*; use the `parse_*` helpers for structured
values such as vectors and rotators.
"""
from __future__ import annotations

import math
import re
from dataclasses import dataclass, field

_BEGIN = re.compile(r'^\s*Begin\s+(\w+)(.*)$')
_END = re.compile(r'^\s*End\s+(\w+)\s*$')
# keys may carry an array index: MiniMapTex(0)=..., MultiSkins(1)=...
_KV = re.compile(r'(\w+(?:\(\d+\))?)=("[^"]*"|\([^()]*(?:\([^()]*\)[^()]*)*\)|\S+)')


@dataclass
class Block:
    kind: str                       # Actor, Brush, PolyList, Polygon, StaticMesh, Triangle, Map...
    header: dict                    # key=value pairs on the Begin line (Class=, Name=, Texture= ...)
    props: dict = field(default_factory=dict)
    lines: list[str] = field(default_factory=list)   # non key=value lines (Vertex ..., Origin ...)
    children: list['Block'] = field(default_factory=list)

    @property
    def cls(self) -> str:
        return self.header.get('Class', '')

    @property
    def name(self) -> str:
        return self.header.get('Name', '')

    def find(self, kind: str) -> list['Block']:
        return [c for c in self.children if c.kind == kind]


def read_text(path) -> str:
    """UCC writes T3D as Latin-1, or as UTF-16 (with a BOM) when a name or text in the map
    needs characters outside Latin-1 (Polish map titles, for example)."""
    data = path.read_bytes()
    if data[:2] in (b'\xff\xfe', b'\xfe\xff'):
        return data.decode('utf-16')
    return data.decode('latin-1')


def parse(text: str) -> Block:
    root = Block('Root', {})
    stack = [root]
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        m = _BEGIN.match(line)
        if m:
            blk = Block(m.group(1), dict(_KV.findall(m.group(2))))
            stack[-1].children.append(blk)
            stack.append(blk)
            continue
        if _END.match(line):
            if len(stack) > 1:
                stack.pop()
            continue
        cur = stack[-1]
        if cur.kind in ('Polygon', 'Triangle') or not '=' in line.split(' ', 1)[0]:
            cur.lines.append(line)
            continue
        # Actor property lines: "Key=Value" (a few lines hold several pairs, e.g. StaticMesh Version=..)
        for k, v in _KV.findall(line):
            if k in cur.props:
                # repeated key without index (rare): keep a list
                old = cur.props[k]
                cur.props[k] = old + [v] if isinstance(old, list) else [old, v]
            else:
                cur.props[k] = v
    return root


# --- value helpers -----------------------------------------------------------

def parse_struct(value: str) -> dict:
    """'(X=1.0,Y=2,Z=3)' -> {'X': '1.0', 'Y': '2', 'Z': '3'} (nested parens kept raw)."""
    value = value.strip()
    if value.startswith('(') and value.endswith(')'):
        value = value[1:-1]
    out, depth, key, cur = {}, 0, None, ''
    for ch in value + ',':
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            if '=' in cur:
                k, v = cur.split('=', 1)
                out[k.strip()] = v.strip()
            cur = ''
            continue
        cur += ch
    return out


def parse_vector(value: str | None, default=(0.0, 0.0, 0.0)) -> tuple[float, float, float]:
    if not value:
        return default
    d = parse_struct(value)
    return (float(d.get('X', default[0])), float(d.get('Y', default[1])), float(d.get('Z', default[2])))


def parse_rotator(value: str | None) -> tuple[int, int, int]:
    """Returns (Pitch, Yaw, Roll) in UE1 rotation units (65536 = 360 deg)."""
    if not value:
        return (0, 0, 0)
    d = parse_struct(value)
    return (int(float(d.get('Pitch', 0))), int(float(d.get('Yaw', 0))), int(float(d.get('Roll', 0))))


def parse_object_ref(value: str | None) -> str | None:
    """"StaticMesh'MyLevel.redbench2'" -> 'MyLevel.redbench2'; 'None' -> None."""
    if not value or value == 'None':
        return None
    m = re.match(r"\w+'([^']*)'", value)
    return m.group(1) if m else value


def parse_float(value: str | None, default: float = 0.0) -> float:
    return _float(value) if value not in (None, '') else default


def _float(text: str) -> float:
    """float() that also takes MSVC's printouts of NaN / infinity ('-1.#IND00', '1.#INF00',
    '1.#QNAN0') as written by UE1's exporter; they come out as NaN."""
    try:
        return float(text)
    except ValueError:
        if '#' in text:
            return float('nan')
        raise


def parse_bool(value: str | None, default: bool = False) -> bool:
    if value is None:
        return default
    return value.strip().lower() == 'true'


# --- static mesh -------------------------------------------------------------

@dataclass
class SMTriangle:
    texture: str
    poly_flags: int
    smoothing: int
    verts: list[tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float]]]


def static_mesh_triangles(block: Block) -> list[SMTriangle]:
    """Triangles of a 'Begin StaticMesh' block. Each vertex: (pos, normal, uv)."""
    tris = []
    for t in block.find('Triangle'):
        tex, flags, smooth, verts = '', 0, 0, []
        for line in t.lines:
            parts = line.split()
            if parts[0] == 'Texture':
                tex = parts[1] if len(parts) > 1 else ''
            elif parts[0] == 'PolyFlags':
                flags = int(parts[1])
            elif parts[0] == 'SmoothingMask':
                smooth = int(parts[1])
            elif parts[0] == 'Vertex':
                f = list(map(_float, parts[2:10]))
                if any(v != v for v in f[0:3]):
                    verts = []      # broken position: drop the triangle
                    break
                f[3:6] = [0.0 if v != v else v for v in f[3:6]]   # broken normal: recomputed later
                f[6:8] = [0.0 if v != v else v for v in f[6:8]]
                verts.append(((f[0], f[1], f[2]), (f[3], f[4], f[5]), (f[6], f[7])))
        if len(verts) == 3:
            tris.append(SMTriangle(tex, flags, smooth, verts))
    return tris


_TILTS = ((0.013, 0.007, 0.011), (-0.009, 0.012, -0.005), (0.004, -0.011, 0.008))   # off edges


def _parity_side(tris: list[SMTriangle], flip: dict[int, bool], cache: dict, samples: int = 8) -> float:
    """+1 if a piece wound as `flip` says faces out of the mesh's solid, -1 if into it, 0 if the
    rays can't tell (an open surface, or they disagree). Up to `samples` triangles spread over it
    each cast rays both ways from their middle; at least three must be conclusive and four in
    five of those agree.
    `cache` keeps the mesh's arrays between pieces (the original winding: flips come after)."""
    import numpy as np
    if not cache:
        p = np.array([[v[0] for v in t.verts] for t in tris], dtype=float)
        cache.update(p=p, v0=p[:, 0], e1=p[:, 1] - p[:, 0], e2=p[:, 2] - p[:, 0])
    p, v0, e1, e2 = cache['p'], cache['v0'], cache['e1'], cache['e2']

    def crossings(origin, d) -> int:   # Möller-Trumbore against every triangle
        h = np.cross(d, e2)
        a = (e1 * h).sum(1)
        ok = np.abs(a) > 1e-9
        f = np.where(ok, 1.0 / np.where(ok, a, 1.0), 0.0)
        s = origin - v0
        u = f * (s * h).sum(1)
        q = np.cross(s, e1)
        v = f * (q * d).sum(1)
        t = f * (e2 * q).sum(1)
        return int((ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1e-3)).sum())

    out = into = 0
    members = sorted(flip)   # not the walk's order: that follows the current winding
    for i in members[::max(1, len(members) // samples)][:samples]:
        n = np.cross(e1[i], e2[i]) * (-1.0 if flip[i] else 1.0)
        length = np.linalg.norm(n)
        if length < 1e-9:
            continue
        n /= length
        mid = p[i].mean(0)
        for tilt in _TILTS:
            d = n + np.array(tilt)
            d /= np.linalg.norm(d)
            ahead, behind = crossings(mid + n * 0.01, d) % 2, crossings(mid - n * 0.01, -d) % 2
            out += ahead == 0 and behind == 1
            into += ahead == 1 and behind == 0
    if out + into >= 3 and out >= 4 * into:
        return 1.0
    if out + into >= 3 and into >= 4 * out:
        return -1.0
    return 0.0


def orient_triangles(tris: list[SMTriangle]) -> list[SMTriangle]:
    """Wind each connected piece of a static mesh one way: neighbours cross a shared edge in
    opposite directions, and the piece as a whole faces along its vertex normals.

    Vertex normals are smoothed, so one triangle's can point away from its own face: going by
    them triangle by triangle turned some round, and the renderer culled those."""
    def key(p):
        return (round(p[0], 2), round(p[1], 2), round(p[2], 2))

    edges: dict[tuple, list[tuple[int, bool]]] = {}
    for i, tri in enumerate(tris):
        ps = [key(v[0]) for v in tri.verts]
        for a, b in ((0, 1), (1, 2), (2, 0)):
            if ps[a] != ps[b]:
                edges.setdefault((min(ps[a], ps[b]), max(ps[a], ps[b])), []).append((i, ps[a] < ps[b]))
    links: list[list[tuple[int, bool]]] = [[] for _ in tris]
    for pair in edges.values():
        if len(pair) == 2 and pair[0][0] != pair[1][0]:   # three or more: not a surface to follow
            (i, up_i), (j, up_j) = pair
            same = up_i == up_j   # both cross it the same way: one of them must turn
            links[i].append((j, same))
            links[j].append((i, same))
    seen = [False] * len(tris)
    rays: dict = {}
    for start in range(len(tris)):
        if seen[start]:
            continue
        seen[start] = True
        flip = {start: False}
        queue = [start]
        for i in queue:
            for j, differs in links[i]:
                if not seen[j]:
                    seen[j] = True
                    flip[j] = flip[i] != differs
                    queue.append(j)
        agreement = cosines = areas = 0.0
        for i, turned in flip.items():
            p = [v[0] for v in tris[i].verts]
            u = [p[1][k] - p[0][k] for k in range(3)]
            w = [p[2][k] - p[0][k] for k in range(3)]
            n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
            avg = [sum(v[1][k] for v in tris[i].verts) for k in range(3)]
            along = sum(n[k] * avg[k] for k in range(3)) * (-1.0 if turned else 1.0)
            agreement += along
            length = math.sqrt(sum(c * c for c in avg))
            if length > 1e-9:
                cosines += along / length
                areas += math.sqrt(sum(c * c for c in n))
        # Normals that hardly agree (smoothed round the corners of overlapping boxes, e.g.
        # mf-waterfront's bench rings): ask the shape instead. From a face of a closed solid a ray
        # outwards crosses it an even number of times, inwards an odd one
        if areas > 0.0 and abs(cosines / areas) < 0.5:
            side = _parity_side(tris, flip, rays)
            if side:
                agreement = side
        for i, turned in flip.items():
            if turned != (agreement < 0):
                v = tris[i].verts
                tris[i].verts = [v[0], v[2], v[1]]
    # A triangle whose three normals all point straight away from its face is wound backwards
    # whatever its piece voted (mf-BraveDwarfes' tree house floors): smoothing can't do that.
    # Backwards, a floor is culled from above and Recast sees it facing down (no navmesh)
    for tri in tris:
        p = [v[0] for v in tri.verts]
        u = [p[1][k] - p[0][k] for k in range(3)]
        w = [p[2][k] - p[0][k] for k in range(3)]
        n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        length = math.sqrt(sum(c * c for c in n))
        if length < 1e-9:
            continue
        if all(sum(n[k] * v[1][k] for k in range(3)) < -0.9 * length * math.sqrt(sum(c * c for c in v[1])) for v in tri.verts):
            v = tri.verts
            tri.verts = [v[0], v[2], v[1]]
    return tris


@dataclass
class BrushPoly:
    texture: str
    flags: int
    origin: tuple[float, float, float]
    normal: tuple[float, float, float]
    texture_u: tuple[float, float, float]
    texture_v: tuple[float, float, float]
    pan_u: int
    pan_v: int
    vertices: list[tuple[float, float, float]]


def brush_polygons(brush: Block) -> list[BrushPoly]:
    """Polygons of a 'Begin Brush' block (Begin PolyList / Begin Polygon Texture= Flags= with
    Origin, Normal, Pan U= V=, TextureU, TextureV, Vertex lines), in the brush's own space."""
    out = []
    for plist in brush.find('PolyList'):
        for poly in plist.find('Polygon'):
            vec = {}
            pan_u = pan_v = 0
            verts = []
            for line in poly.lines:
                parts = line.split(None, 1)
                if len(parts) < 2:
                    continue
                key, rest = parts
                if key == 'Pan':
                    kv = dict(_KV.findall(rest))
                    pan_u, pan_v = int(kv.get('U', 0)), int(kv.get('V', 0))
                elif key == 'Vertex':
                    verts.append(tuple(_float(c) for c in rest.split(',')))
                else:
                    vec[key] = tuple(_float(c) for c in rest.split(','))
            if len(verts) >= 3:
                out.append(BrushPoly(poly.header.get('Texture', ''), int(poly.header.get('Flags', 0)),
                                     vec.get('Origin', (0.0, 0.0, 0.0)), vec.get('Normal', (0.0, 0.0, 1.0)),
                                     vec.get('TextureU', (1.0, 0.0, 0.0)), vec.get('TextureV', (0.0, 1.0, 0.0)),
                                     pan_u, pan_v, verts))
    return out
