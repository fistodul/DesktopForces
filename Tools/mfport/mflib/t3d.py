"""Parse UE1 / Mobile Forces T3D text (as written by `ucc batchexport ... Level t3d` and `StaticMesh t3d`).

The result is a light tree of `Block`s (Begin X ... End X) whose `props` dict maps
property names to *raw text values*; use the `parse_*` helpers for structured
values such as vectors and rotators.
"""
from __future__ import annotations

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
