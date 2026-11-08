"""Twin maps: two releases of one map built on the same coordinates.

map_edits.toml `layout_from = "<other map>"`: this map is converted from the other's layout
(world, static meshes, movers, gameplay) and keeps only its own look: lights, ambient
sounds, the zones' fog and ambient light, its LevelInfo (title, music) and its sky box. One
layout, lit two ways (MF-BattleBayDarkV1 = mf-battlebaynoonv1's at night).

`extra_polygons` + `pure_additions` (debug/twin_diff.py) tell whether one release has world
geometry the other lacks: the other's polygons minus the coplanar surfaces this one has
(exact convex clipping, so the two BSPs' different splits don't count), facing this one's
open space, and not where this one changed too (there the two disagree: a redesign).
"""
from __future__ import annotations

import math
from dataclasses import replace

from .bsp import LevelModel, WorldPoly

PLANE_TOL = 1.0     # uu: polygons this close to one plane and facing the same way coincide
MIN_AREA = 4.0      # uu^2: clipped slivers smaller than this are dropped
CHANGE_REACH = 128  # uu: an addition this close to the other release's own changes is a redesign


# --- look swap (layout_from) ------------------------------------------------------------
def _is_light(tree, cls: str) -> bool:
    return cls.endswith('Light') or tree.is_a(cls, 'Light')


def _is_look_key(key: str) -> bool:
    return 'Fog' in key or key.startswith('Ambient')


def relight(layout: list[dict], look: list[dict], tree) -> list[dict]:
    """The layout's actors with the look map's lights, ambient sounds, LevelInfo and zone looks.
    A zone the look map doesn't have (noon's captain zones) takes its main zone's look."""
    def own(a):
        return _is_light(tree, a['class']) or a['class'] == 'AmbientSound'
    by_name = {a['name']: a for a in look}
    main = next(a for a in look if a['class'] == 'LevelInfo')
    out = []
    for a in layout:
        if own(a):
            continue
        a = dict(a, props=dict(a['props']))
        if a['class'] == 'LevelInfo':
            a['props'] = dict(main['props'])
        elif tree.is_a(a['class'], 'ZoneInfo') or a['class'].endswith('ZoneInfo'):
            src = by_name.get(a['name'])
            if not src or src['class'] != a['class']:
                src = main
            elif a['class'] == 'SkyZoneInfo' or tree.is_a(a['class'], 'SkyZoneInfo'):
                a['location'] = src['location']   # its sky box comes along (sky_box)
            a['props'] = {k: v for k, v in a['props'].items() if not _is_look_key(k)}
            a['props'].update({k: v for k, v in src['props'].items() if _is_look_key(k)})
        out.append(a)
    taken = {a['name'] for a in out}
    for a in look:
        if own(a):
            out.append(dict(a, name=a['name'] if a['name'] not in taken else f"Look_{a['name']}"))
    return out


def sky_box(model: LevelModel, look_model: LevelModel, actors: list[dict], look_actors: list[dict], tree) -> list[WorldPoly]:
    """The layout's world polygons with the look map's sky box in place of its own."""
    def sky_zones(m, acts):
        cls = {a['name']: a['class'] for a in acts}
        return [i for i, n in enumerate(m.zones) if n and (cls.get(n) == 'SkyZoneInfo' or tree.is_a(cls.get(n, ''), 'SkyZoneInfo'))]
    mine, theirs = sky_zones(model, actors), sky_zones(look_model, look_actors)
    polys = model.polygons()
    if len(mine) != 1 or len(theirs) != 1:
        print(f'  layout: sky zones {mine} / {theirs}: the layout keeps its own sky box')
        return polys
    s, t = mine[0], theirs[0]
    kept = [p for p in polys if p.zone != s]
    box = [replace(p, zone=s, back_zone=s if p.back_zone == t else 0) for p in look_model.polygons() if p.zone == t]
    print(f'  layout: sky box {len(polys) - len(kept)} -> {len(box)} polygons')
    return kept + box


# --- geometry diff (debug/twin_diff.py) ------------------------------------------------------
def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _unit(v):
    n = math.sqrt(_dot(v, v)) or 1.0
    return (v[0] / n, v[1] / n, v[2] / n)


def _basis(n):
    a = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
    u = _unit(_cross(a, n))
    return u, _cross(n, u)


def _area2(p) -> float:
    return sum(p[i][0] * p[(i + 1) % len(p)][1] - p[(i + 1) % len(p)][0] * p[i][1] for i in range(len(p))) / 2.0


def _ccw(p):
    return p if _area2(p) >= 0 else p[::-1]


def _clip(poly, a, b, keep_left: bool):
    """Sutherland-Hodgman against the line a->b: the part on its left (or right)."""
    def side(q):
        s = (b[0] - a[0]) * (q[1] - a[1]) - (b[1] - a[1]) * (q[0] - a[0])
        return s if keep_left else -s
    out = []
    for i, cur in enumerate(poly):
        nxt = poly[(i + 1) % len(poly)]
        sc, sn = side(cur), side(nxt)
        if sc >= 0:
            out.append(cur)
        if (sc >= 0) != (sn >= 0):
            t = sc / (sc - sn)
            out.append((cur[0] + (nxt[0] - cur[0]) * t, cur[1] + (nxt[1] - cur[1]) * t))
    return out


def subtract(p, q):
    """Convex polygon p minus convex polygon q (both CCW, 2D): convex pieces."""
    pieces, rest = [], p
    for i in range(len(q)):
        a, b = q[i], q[(i + 1) % len(q)]
        outside = _clip(rest, a, b, keep_left=False)
        if len(outside) >= 3 and _area2(outside) > MIN_AREA:
            pieces.append(outside)
        rest = _clip(rest, a, b, keep_left=True)
        if len(rest) < 3 or _area2(rest) <= MIN_AREA:
            break
    return pieces


def _bounds(vs, margin=0.0):
    return (tuple(min(v[i] for v in vs) - margin for i in range(3)), tuple(max(v[i] for v in vs) + margin for i in range(3)))


def _overlap(a, b) -> bool:
    return all(a[0][i] <= b[1][i] and b[0][i] <= a[1][i] for i in range(3))


def extra_polygons(base: LevelModel, base_polys: list[WorldPoly], other_polys: list[WorldPoly]) -> list[WorldPoly]:
    """The other release's polygons (pieces of them) that base has no surface for, facing base's
    open space (BSP zone 0 is solid)."""
    planes: dict[tuple, list] = {}
    for p in base_polys:
        n = _unit(p.normal)
        planes.setdefault(tuple(round(c, 2) for c in n), []).append((_dot(n, p.vertices[0]), _bounds(p.vertices, PLANE_TOL), p))
    out = []
    for poly in other_polys:
        n = _unit(poly.normal)
        o = poly.vertices[0]
        d = _dot(n, o)
        u, v = _basis(n)
        def to2(p):
            return (_dot(_sub(p, o), u), _dot(_sub(p, o), v))
        box = _bounds(poly.vertices)
        pieces = [_ccw([to2(p) for p in poly.vertices])]
        for d2, box2, q in planes.get(tuple(round(c, 2) for c in n), ()):
            if abs(d2 - d) <= PLANE_TOL and _overlap(box, box2):
                pieces = [r for p in pieces for r in subtract(p, _ccw([to2(x) for x in q.vertices]))]
                if not pieces:
                    break
        for piece in pieces:
            pts = [(o[0] + u[0] * x + v[0] * y, o[1] + u[1] * x + v[1] * y, o[2] + u[2] * x + v[2] * y) for x, y in piece]
            c = tuple(sum(p[i] for p in pts) / len(pts) for i in range(3))
            if base.zone_at(tuple(c[i] + n[i] * 2 for i in range(3))):
                out.append(replace(poly, vertices=pts))
    return out


def pure_additions(extra: list[WorldPoly], reverse: list[WorldPoly]) -> list[WorldPoly]:
    """extra (what B has that A lacks) away from reverse (what A has that B lacks)."""
    changed = [_bounds(p.vertices, CHANGE_REACH) for p in reverse]
    return [p for p in extra if not any(_overlap(_bounds(p.vertices), c) for c in changed)]
