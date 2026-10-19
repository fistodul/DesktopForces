"""Find seams in a merged map: places on an MF/MFS border where a walkable floor on one
side has no floor within a step's height on the other (gap, hole or height mismatch).

Floors (up-facing collision triangles) are rasterised into a grid; each cell knows which
version owns it (merge preset). Border cells are compared with their neighbours across the
border. Writes a list of seam clusters (centre, size, kind, heights) and a map image.

Usage: python3 seams.py mf-warehouse [--cell 50] [--out work/merge/seams]
"""
import argparse
import json
import math
import sys
from collections import defaultdict
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mflib import config  # noqa: E402
from mflib.merge import Merger  # noqa: E402
from mflib.mfmesh import read  # noqa: E402

STEP = 60.0      # cm: MF step height 25 uu ~ 56 cm
HEADROOM = 400.0  # floors closer than this vertically are "the same storey"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('preset')
    ap.add_argument('--cell', type=float, default=50.0)
    ap.add_argument('--out', default='work/merge/seams')
    a = ap.parse_args()
    cfg = config.load()
    m = Merger(cfg, a.preset)
    root = m.out
    lvl = json.loads((root / 'level.json').read_text())
    c = a.cell
    floors = defaultdict(list)        # (i, j) -> [z]
    walls = defaultdict(list)         # (i, j) -> [(zmin, zmax)]
    for w in lvl['world']:
        if not w['mesh'].split('/')[-1].split('_', 1)[1].startswith('Collision'):
            continue
        mb = read(root / f"{w['mesh']}.mfmesh.json")
        ox, oy, oz = w['location']
        for tris in mb.tris.values():
            for tri in tris:
                p = [(v[0][0] + ox, v[0][1] + oy, v[0][2] + oz) for v in tri]
                n = tri[0][1]
                if abs(n[2]) < 0.3:
                    # wall: mark the cells along its foot print with its height range
                    zlo, zhi = min(q[2] for q in p), max(q[2] for q in p)
                    for k in range(3):
                        a0, a1 = p[k], p[(k + 1) % 3]
                        steps = int(math.hypot(a1[0] - a0[0], a1[1] - a0[1]) / (c * 0.5)) + 1
                        for t in range(steps + 1):
                            x = a0[0] + (a1[0] - a0[0]) * t / steps
                            y = a0[1] + (a1[1] - a0[1]) * t / steps
                            walls[(math.floor(x / c), math.floor(y / c))].append((zlo, zhi))
                    continue
                if n[2] < 0.7:
                    continue
                # plane z = z0 + gx*x + gy*y
                (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = p
                det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
                if abs(det) < 1e-6:
                    continue
                for i in range(math.floor(min(x0, x1, x2) / c), math.floor(max(x0, x1, x2) / c) + 1):
                    for j in range(math.floor(min(y0, y1, y2) / c), math.floor(max(y0, y1, y2) / c) + 1):
                        px, py = (i + 0.5) * c, (j + 0.5) * c
                        l1 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / det
                        l2 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / det
                        l3 = 1 - l1 - l2
                        if min(l1, l2, l3) >= -0.01:
                            floors[(i, j)].append(l1 * z0 + l2 * z1 + l3 * z2)
    owner = {}
    for (i, j), zs in floors.items():
        owner[(i, j)] = [m.owner(((i + 0.5) * c, (j + 0.5) * c, z + 50.0)) for z in zs]

    seams = []   # (x, y, z, kind)
    for (i, j), zs in floors.items():
        for z, o in zip(zs, owner[(i, j)]):
            for di, dj in ((1, 0), (0, 1), (-1, 0), (0, -1)):
                nb = (i + di, j + dj)
                if m.owner(((nb[0] + 0.5) * c, (nb[1] + 0.5) * c, z + 50.0)) == o:
                    continue                      # not a border
                # a real floor reaching the border: continues back from it and to both sides
                # (skips wall tops, ledges and other thin strips)
                def has(cell):
                    return any(abs(fz - z) <= STEP for fz in floors.get(cell, ()))
                if not all(has(cell) for cell in ((i - di, j - dj), (i - 2 * di, j - 2 * dj),
                                                  (i + dj, j + di), (i - dj, j - di))):
                    continue
                body = z + 100.0
                if any(lo <= body <= hi for cell in ((i, j), nb) for lo, hi in walls.get(cell, ())):
                    continue                      # a wall closes the border here
                near = [nz for nz in floors.get(nb, []) if abs(nz - z) < HEADROOM]
                if not near:
                    seams.append(((i + 0.5) * c, (j + 0.5) * c, z, 'edge'))   # floor ends at the border
                elif min(abs(nz - z) for nz in near) > STEP:
                    seams.append(((i + 0.5) * c, (j + 0.5) * c, z, 'step'))   # height mismatch
                break
    # cluster seam cells (3 m)
    clusters = []
    for x, y, z, kind in seams:
        for cl in clusters:
            if cl['kind'] == kind and abs(cl['x'] - x) < 300 and abs(cl['y'] - y) < 300 and abs(cl['z'] - z) < 200:
                cl['n'] += 1
                cl['x'] += (x - cl['x']) / cl['n']
                cl['y'] += (y - cl['y']) / cl['n']
                break
        else:
            clusters.append({'x': x, 'y': y, 'z': z, 'kind': kind, 'n': 1})
    clusters = [cl for cl in clusters if cl['n'] >= 3]
    clusters.sort(key=lambda cl: -cl['n'])
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'seams.json').write_text(json.dumps(clusters, indent=1))

    # picture: floors by owner, seams in red/yellow
    xs = [k[0] for k in floors]
    ys = [k[1] for k in floors]
    i0, j0 = min(xs), min(ys)
    W, H = max(xs) - i0 + 1, max(ys) - j0 + 1
    img = Image.new('RGB', (W, H), (10, 10, 14))
    px = img.load()
    for (i, j), os_ in owner.items():
        px[i - i0, j - j0] = (60, 110, 230) if 'MF' in os_ and 'MFS' not in os_ else (230, 140, 40) if 'MFS' in os_ and 'MF' not in os_ else (160, 160, 160)
    d = ImageDraw.Draw(img)
    for cl in clusters:
        x, y = cl['x'] / c - i0, cl['y'] / c - j0
        r = 2 + math.sqrt(cl['n'])
        d.ellipse([x - r, y - r, x + r, y + r], outline=(255, 40, 40) if cl['kind'] == 'edge' else (255, 255, 0))
    img = img.resize((W * 2, H * 2), Image.NEAREST)
    img.save(out / 'seams.png')
    print(f'{len(seams)} seam cells in {len(clusters)} clusters -> {out}')
    for cl in clusters[:25]:
        print(f"  {cl['kind']:4} n={cl['n']:4}  at ({cl['x']:.0f}, {cl['y']:.0f}, {cl['z']:.0f}) cm")


if __name__ == '__main__':
    main()
