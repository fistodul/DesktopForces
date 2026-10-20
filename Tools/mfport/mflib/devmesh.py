"""Rage `DeviceMesh` reader (vehicles, loadout crates, safes, flag bases, detonation locks ...).

DeviceMesh is Rage's own static vertex-buffer mesh; UCC has no exporter for it. Its native
data ends with what matters, so it is read from the back:

    ... header (bounds, Rage runtime fields) ...
    compact  NumSections
      per section: compact Material (texture ref), int32 ?, int32 FirstIndex,
                   int32 NumPrimitives, int32 PrimType (0 triangle list, 1 strip),
                   int32 MinVertex, int32 MaxVertex
    compact  NumIndices,  uint16 x NumIndices
    compact  NumVertices, 44 bytes each:
             float X, Y, Z (uu), float 0, float NX, NY, NZ, float U, V, 8 bytes runtime junk

Verified on LoadoutCrateBlueMesh (1 strip section) and BuggyMesh (2 list sections, 3897
indices, 3453 vertices, the array ending exactly at the end of the export).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

from .upkg import Package, Reader

VERTEX_SIZE = 44


@dataclass
class DMSection:
    material: str | None
    first_index: int
    num_prims: int
    prim_type: int          # 0 = triangle list, 1 = triangle strip
    min_vertex: int
    max_vertex: int


@dataclass
class DeviceMesh:
    name: str
    sections: list[DMSection] = field(default_factory=list)
    indices: list[int] = field(default_factory=list)
    # (pos, normal, uv) in MF units / texture space
    vertices: list[tuple] = field(default_factory=list)

    def triangles(self):
        """(section, (i0, i1, i2)) for every non-degenerate triangle."""
        for sec in self.sections:
            idx = self.indices[sec.first_index:]
            if sec.prim_type == 1:
                for k in range(sec.num_prims):
                    a, b, c = idx[k], idx[k + 1], idx[k + 2]
                    if a == b or b == c or a == c:
                        continue
                    yield sec, ((a, b, c) if k % 2 == 0 else (a, c, b))
            else:
                for k in range(sec.num_prims):
                    yield sec, (idx[3 * k], idx[3 * k + 1], idx[3 * k + 2])


def _compact_len(data: bytes, pos: int) -> int:
    """Byte length of the UE compact index starting at pos."""
    n = 1
    if data[pos] & 0x40:
        while n < 5 and data[pos + n] & 0x80:
            n += 1
        n += 1
    return n


def _tail_arrays(data: bytes, end: int, elem: int) -> list[tuple[int, int]]:
    """Every `compact N` (N >= 1) + N*elem bytes ending exactly at `end`, biggest N first."""
    found = []
    for size in range(1, 6):
        for n in range(1, (end - size) // elem + 1):
            pos = end - size - n * elem
            if _compact_len(data, pos) == size and Reader(data, pos).index() == n:
                found.append((pos, n))
    return sorted(found, key=lambda c: -c[1])


def read(pkg: Package, name: str) -> DeviceMesh | None:
    exp = pkg.find(name, 'DeviceMesh')
    if exp is None:
        return None
    obj = pkg.read_object(exp)
    data = pkg.data[obj.native_offset:exp.offset + exp.size]
    found = None
    for vpos, nverts in _tail_arrays(data, len(data), VERTEX_SIZE)[:4]:
        for ipos, nidx in _tail_arrays(data, vpos, 2)[:8]:
            sections = _find_sections(pkg, data, ipos, nverts, nidx)
            if sections:
                found = (vpos, nverts, ipos, nidx, sections)
                break
        if found:
            break
    if not found:
        return None
    vpos, nverts, ipos, nidx, sections = found
    mesh = DeviceMesh(name, sections)
    r = Reader(data, ipos)
    r.index()
    mesh.indices = list(struct.unpack_from(f'<{nidx}H', data, r.p))
    r = Reader(data, vpos)
    r.index()
    for i in range(nverts):
        x, y, z, _pad, nx, ny, nz, u, v = struct.unpack_from('<9f', data, r.p + i * VERTEX_SIZE)
        mesh.vertices.append(((x, y, z), (nx, ny, nz), (u, v)))
    return mesh


def _find_sections(pkg: Package, data: bytes, end: int, nverts: int, nidx: int) -> list[DMSection] | None:
    """The section table ends where the index array starts; search back for a count that fits."""
    for start in range(end - 26, max(0, end - 26 * 64 - 8), -1):
        try:
            r = Reader(data, start)
            count = r.index()
            if not 1 <= count <= 64:
                continue
            secs = []
            for _ in range(count):
                mat = r.index()
                _x, first, prims, ptype, vmin, vmax = struct.unpack_from('<6i', data, r.p)
                r.p += 24
                if ptype not in (0, 1) or not 0 <= first <= nidx or prims <= 0 or not 0 <= vmin <= vmax < nverts:
                    raise ValueError
                used = prims + 2 if ptype == 1 else prims * 3
                if first + used > nidx:
                    raise ValueError
                secs.append(DMSection(pkg.ref_path(mat) if mat else None, first, prims, ptype, vmin, vmax))
            if r.p == end:
                return secs
        except (ValueError, IndexError, struct.error):
            continue
    return None
