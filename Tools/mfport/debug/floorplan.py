"""Floor plan of a converted map: walkable (upward-facing) surfaces coloured by height,
player starts as dots. Used to align / compare map versions (Docs/WarehouseMerge.md).

Usage: python3 floorplan.py work/out/Maps/<map> out.png [--px-per-m 4] [--zmax 2000]
       [--transform sx,dx,dy,yaw]   (applied to XY before drawing, metres)
"""
import argparse
import colorsys
import json
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw


def load_tris(root: Path, rel: str, offset):
    meta = json.loads((root / f'{rel}.mfmesh.json').read_text())
    data = (root / rel).parent.joinpath(meta['bin']).read_bytes()
    n = meta['vertex_count']
    verts = [struct.unpack_from('<8f', data, i * 32) for i in range(n)]
    idx = struct.unpack_from(f"<{meta['index_count']}I", data, n * 32)
    for t in range(0, len(idx), 3):
        a, b, c = (verts[idx[t + k]] for k in range(3))
        yield [(v[0] + offset[0], v[1] + offset[1], v[2] + offset[2]) for v in (a, b, c)], (a[3], a[4], a[5])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('map_dir')
    ap.add_argument('out')
    ap.add_argument('--px-per-m', type=float, default=4.0)
    ap.add_argument('--zmax', type=float, default=1e9, help='ignore surfaces above this Z (cm)')
    ap.add_argument('--transform', default='1,0,0,0', help='scale,dx_m,dy_m,yaw_deg')
    a = ap.parse_args()
    root = Path(a.map_dir)
    lvl = json.loads((root / 'level.json').read_text())
    s, dx, dy, yaw = map(float, a.transform.split(','))
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))

    def tx(p):
        x, y = p[0] / 100 * s, p[1] / 100 * s
        return (x * cy - y * sy + dx, x * sy + y * cy + dy)

    polys = []
    for w in lvl['world']:
        if not w['render']:
            continue
        for tri, n in load_tris(root, w['mesh'], w['location']):
            if n[2] > 0.7 and max(v[2] for v in tri) < a.zmax:
                polys.append((sum(v[2] for v in tri) / 3, [tx(v) for v in tri]))
    if not polys:
        raise SystemExit('no floor polygons')
    xs = [p[0] for _, t in polys for p in t]
    ys = [p[1] for _, t in polys for p in t]
    zs = [z for z, _ in polys]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    z0, z1 = min(zs), max(zs)
    k = a.px_per_m
    img = Image.new('RGB', (int((x1 - x0) * k) + 20, int((y1 - y0) * k) + 20), (20, 20, 20))
    d = ImageDraw.Draw(img)
    for z, t in sorted(polys, key=lambda p: p[0]):
        h = 0.7 * (1 - (z - z0) / max(1, z1 - z0))
        col = tuple(int(c * 255) for c in colorsys.hsv_to_rgb(h, 0.7, 0.9))
        d.polygon([((p[0] - x0) * k + 10, (p[1] - y0) * k + 10) for p in t], fill=col)
    for r in lvl.get('spawns', []):
        if r.get('type') == 'actor' and 'PlayerStart' in r.get('class', ''):
            x, y = tx(r['location'])
            team = r['props'].get('TeamNumber', '255')
            col = {'0': (60, 120, 255), '1': (255, 60, 60)}.get(team, (255, 255, 255))
            d.ellipse([(x - x0) * k + 6, (y - y0) * k + 6, (x - x0) * k + 14, (y - y0) * k + 14], fill=col)
    d.text((12, 12), f'{root.name}  x {x0:.0f}..{x1:.0f} m  y {y0:.0f}..{y1:.0f} m  z {z0/100:.1f}..{z1/100:.1f} m', fill=(255, 255, 255))
    img.save(a.out)
    print(f'{a.out}: {img.size}, x {x0:.1f}..{x1:.1f}, y {y0:.1f}..{y1:.1f} m')


if __name__ == '__main__':
    main()
