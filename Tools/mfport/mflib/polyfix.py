"""Make BSP polygons watertight and consistently wound for GPU rendering.

UE1's BSP splits polygons wherever brushes cut them, which leaves T-junctions: a vertex of
one polygon lying in the middle of a neighbour's edge. A GPU rasterises the two sides of
such an edge slightly differently, so pixel-wide cracks into the void appear. The software
renderer UE1 shipped with hid them; a modern renderer doesn't.

- `weld_tjunctions` inserts those vertices into the neighbours' edges.
- `newell_normal` gives a polygon's facing from all its vertices (the first three are often
  collinear after BSP splits, so their cross product's sign is noise).
- `triangulate` fans from a corner whose two sides are unsplit, else from the centroid, so
  every inserted vertex really joins a triangle edge (a fan from a vertex on a split side
  would only make zero-area triangles there and keep the crack).
"""
from __future__ import annotations

import math
from collections import defaultdict

Vec = tuple[float, float, float]


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def newell_normal(vs: list[Vec]) -> Vec:
    n = [0.0, 0.0, 0.0]
    for i, a in enumerate(vs):
        b = vs[(i + 1) % len(vs)]
        n[0] += (a[1] - b[1]) * (a[2] + b[2])
        n[1] += (a[2] - b[2]) * (a[0] + b[0])
        n[2] += (a[0] - b[0]) * (a[1] + b[1])
    return (n[0], n[1], n[2])


def weld_tjunctions(polys: list[list[Vec]], tol: float = 0.25, cell: float = 128.0) -> tuple[list[list[Vec]], int]:
    """Insert every polygon vertex that lies inside another polygon's edge (within `tol`,
    in the polygons' units) into that edge. Returns the new vertex lists and the count."""
    grid: dict[tuple, set] = defaultdict(set)
    for vs in polys:
        for p in vs:
            grid[(math.floor(p[0] / cell), math.floor(p[1] / cell), math.floor(p[2] / cell))].add(p)
    out, inserted = [], 0
    for vs in polys:
        nv = []
        for i, a in enumerate(vs):
            b = vs[(i + 1) % len(vs)]
            nv.append(a)
            d = _sub(b, a)
            ll = _dot(d, d)
            if ll < 4 * tol * tol:
                continue
            lo = [math.floor((min(a[k], b[k]) - tol) / cell) for k in range(3)]
            hi = [math.floor((max(a[k], b[k]) + tol) / cell) for k in range(3)]
            found = []
            for x in range(lo[0], hi[0] + 1):
                for y in range(lo[1], hi[1] + 1):
                    for z in range(lo[2], hi[2] + 1):
                        for p in grid.get((x, y, z), ()):
                            ap = _sub(p, a)
                            t = _dot(ap, d) / ll
                            if t <= 0.0 or t >= 1.0:
                                continue
                            e = (ap[0] - d[0] * t, ap[1] - d[1] * t, ap[2] - d[2] * t)
                            if _dot(e, e) > tol * tol:
                                continue
                            # skip points (almost) on the edge's own end points
                            if t * t * ll < tol * tol or (1 - t) * (1 - t) * ll < tol * tol:
                                continue
                            found.append((t, p))
            if found:
                found.sort()
                last = None
                for t, p in found:
                    if last is None or _dot(_sub(p, last), _sub(p, last)) > tol * tol:
                        nv.append(p)
                        last = p
                        inserted += 1
        out.append(nv)
    return out, inserted


def _collinear(a, b, c, tol: float) -> bool:
    """b lies on the segment line a-c (within tol)."""
    d = _sub(c, a)
    ll = _dot(d, d)
    if ll == 0.0:
        return True
    ab = _sub(b, a)
    t = _dot(ab, d) / ll
    e = (ab[0] - d[0] * t, ab[1] - d[1] * t, ab[2] - d[2] * t)
    return _dot(e, e) <= tol * tol


def triangulate(vs: list, pos=lambda v: v, tol: float = 0.05) -> tuple[list, list[tuple[int, int, int]]]:
    """Triangles (index triples) for a convex polygon whose sides may hold extra vertices.
    Returns (vertices, triangles); a centroid vertex may be appended (callers interpolate
    their attributes through `pos`-compatible items, so this returns it as a position)."""
    n = len(vs)
    if n == 3:
        return vs, [(0, 1, 2)]
    corner = [not _collinear(pos(vs[i - 1]), pos(vs[i]), pos(vs[(i + 1) % n]), tol) for i in range(n)]
    for k in range(n):
        if corner[k] and corner[k - 1] and corner[(k + 1) % n]:
            return vs, [(k, (k + j) % n, (k + j + 1) % n) for j in range(1, n - 1)]
    # every corner touches a split side: fan from the centroid instead
    c = tuple(sum(pos(v)[i] for v in vs) / n for i in range(3))
    return vs + [c], [(n, i, (i + 1) % n) for i in range(n)]
