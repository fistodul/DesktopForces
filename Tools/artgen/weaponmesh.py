#!/usr/bin/env python3
"""The core game's own weapon models: hudart.py's side views given thickness.

Without a Mobile Forces install the weapons would be grey boxes. These stand in for them: every
outline of the HUD icon, extruded and scaled to the size of the original's carry model (sizes in
SIZES, so the view placement and pickups fit), one material (T_MF_WeaponMetal). Named like the
originals, which win when installed (DF::LoadClassicObject). MPL-2.0 like the code.

    python3 Tools/artgen/weaponmesh.py     ->  Tools/artgen/out/Meshes/Weapons/*.mfmesh.*
                                               Tools/artgen/out/Art/Weapons/T_MF_WeaponMetal.png
    python3 Tools/mfport/mfport.py import --steps hud
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import hudart  # noqa: E402
import lookalike  # noqa: E402
sys.path.insert(0, str(HERE.parent / 'mfport'))
from mflib.mfmesh import MeshBuilder  # noqa: E402
sys.path.pop(0)                                     # mfport.py would hide the mfport package

OUT = HERE / 'out' / 'Meshes' / 'Weapons'
TEXTURE = HERE / 'out' / 'Art' / 'Weapons' / 'T_MF_WeaponMetal.png'
MATERIAL = 'Engine.WeaponMetal|opaque'     # replaced by MI_MF_WeaponMetal on import
TILE_CM = 24.0

# mesh name: (outline, size X, Y, Z in cm of the original's model, turn in degrees, flip so the
# muzzle faces +X)
SIZES = {
    'PistolPickupCarryMesh': (hudart.w_pistol, (22.7, 3.4, 14.4), 0, True),
    'MachineGunPickupCarryMesh': (hudart.w_mg, (89.8, 5.6, 33.3), 0, False),
    'ShotgunPickupCarryMesh': (hudart.w_shotgun, (83.7, 5.9, 21.2), 0, False),
    'SniperRiflePickupCarryMesh': (hudart.w_rifle, (115.2, 6.0, 23.0), 0, False),
    'RocketLauncherPickupCarryMesh': (hudart.w_rocket, (156.2, 16.0, 42.8), 0, False),
    'HeavyMachineGunPickupCarryMesh': (hudart.w_hmg, (163.8, 16.0, 32.0), 0, False),
    'KnifePickupCarryMesh': (hudart.w_knife, (6.1, 1.8, 36.5), 54, False),     # held point up
    'TripBombsPickupCarryMesh': (hudart.w_tripbomb, (26.0, 12.0, 20.0), 0, False),
    'GrenadesPickupCarryMesh': (hudart.w_grenade, (10.1, 9.0, 14.0), 0, False),
}


class Outlines:
    """Takes a hudart painter's shapes as point lists instead of pixels."""

    def __init__(self):
        self.shapes: list[list[tuple[float, float]]] = []

    def poly(self, pts, fill=None):
        self.shapes.append([(float(x), float(y)) for x, y in pts])

    def circle(self, cx, cy, r, fill=None, outline=None, width=1.0):
        self.poly([(cx + r * math.cos(i * math.tau / 12), cy + r * math.sin(i * math.tau / 12)) for i in range(12)])


def signed_area(pts) -> float:
    return 0.5 * sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))


def triangulate(pts) -> list[tuple[int, int, int]]:
    """Ear clipping of a simple polygon (counter-clockwise), as indices into pts."""
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    left = list(range(len(pts)))
    out = []
    while len(left) > 3:
        for k, i in enumerate(left):
            a, b, c = left[k - 1], i, left[(k + 1) % len(left)]
            if cross(pts[a], pts[b], pts[c]) <= 0:
                continue
            others = (p for p in left if p not in (a, b, c))
            if any(cross(pts[a], pts[b], pts[p]) >= 0 and cross(pts[b], pts[c], pts[p]) >= 0 and cross(pts[c], pts[a], pts[p]) >= 0 for p in others):
                continue
            out.append((a, b, c))
            left.pop(k)
            break
        else:
            raise ValueError('outline crosses itself')
    out.append(tuple(left))
    return out


def extrude(mesh: MeshBuilder, pts, half: float) -> None:
    """pts in cm, X along the gun, Z up, counter-clockwise seen from -Y (the left side)."""
    def corner(x, y, z, n):
        return ((x, y, z), n, (x / TILE_CM + (0.5 if abs(n[1]) < 0.5 else 0), z / TILE_CM))

    left, right = (0.0, -1.0, 0.0), (0.0, 1.0, 0.0)
    for a, b, c in triangulate(pts):
        # front faces are clockwise seen from the front
        mesh.add_triangle(MATERIAL, *(corner(*pts[i][:1], -half, pts[i][1], left) for i in (a, c, b)))
        mesh.add_triangle(MATERIAL, *(corner(*pts[i][:1], half, pts[i][1], right) for i in (a, b, c)))
    for i, p in enumerate(pts):
        q = pts[(i + 1) % len(pts)]
        dx, dz = q[0] - p[0], q[1] - p[1]
        length = math.hypot(dx, dz) or 1.0
        n = (dz / length, 0.0, -dx / length)     # outward for counter-clockwise
        v = [corner(p[0], -half, p[1], n), corner(q[0], -half, q[1], n), corner(q[0], half, q[1], n), corner(p[0], half, p[1], n)]
        mesh.add_triangle(MATERIAL, v[0], v[2], v[1])
        mesh.add_triangle(MATERIAL, v[0], v[3], v[2])


def build(name: str, paint, size, turn: float, flip: bool) -> MeshBuilder:
    shapes = Outlines()
    paint(shapes, None)
    cos, sin = math.cos(math.radians(turn)), math.sin(math.radians(turn))
    turned = [[(x * cos + y * sin, x * sin - y * cos) for x, y in shape] for shape in shapes.shapes]   # Z up
    xs = [p[0] for s in turned for p in s]
    zs = [p[1] for s in turned for p in s]
    sx, sz = size[0] / (max(xs) - min(xs)), size[2] / (max(zs) - min(zs))
    mx, mz = (min(xs) + max(xs)) / 2, (min(zs) + max(zs)) / 2
    mesh = MeshBuilder(name=name, collision='none')
    for shape in turned:
        pts = [((mx - x if flip else x - mx) * sx, (z - mz) * sz) for x, z in shape]
        if signed_area(pts) < 0:
            pts.reverse()
        extrude(mesh, pts, size[1] / 2)
    return mesh


def metal() -> Image.Image:
    rng = np.random.default_rng(1983)
    base = lookalike.stretched(rng, 256, 256, 8, 1) * 0.15 + lookalike.fbm(rng, 256, 256, 1.2) * 0.85
    v = 0.5 + (base - base.mean()) / base.std() * 0.3
    rgb = lookalike.ramp(v, np.array((70, 72, 80), np.float32), np.array((112, 115, 125), np.float32), np.array((165, 168, 178), np.float32))
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), 'RGB')


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    TEXTURE.parent.mkdir(parents=True, exist_ok=True)
    metal().save(TEXTURE, optimize=True)
    for name, (paint, size, turn, flip) in SIZES.items():
        build(name, paint, size, turn, flip).write(OUT)
    print(f'[weaponmesh] {len(SIZES)} meshes -> {OUT}')


if __name__ == '__main__':
    main()
