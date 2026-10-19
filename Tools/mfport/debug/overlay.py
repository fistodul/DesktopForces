"""Overlay two converted maps' walkable surfaces in one top-down image (Docs/WarehouseMerge.md).

The first map is drawn blue, the second orange; where both have floor the colours mix.
Player starts and objectives of both maps are marked. Both maps must already share a frame
(`mfport.py vmf` applies the alignment from vmf_maps.toml).

Usage: python3 overlay.py work/out/Maps/mf-warehouse work/out/Maps/mfs-warehouse out.png
       [--px-per-m 3] [--zmin -1e9 --zmax 1e9] [--grid 10]
"""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw

from floorplan import load_tris

MARK = {
    'RageDominationTimer': ('H', (255, 255, 0)), 'MFS:prop_holdout': ('H', (255, 255, 0)),
    'RageFlagBase': ('F', (255, 255, 255)), 'RageSafe': ('S', (255, 255, 255)),
    'RageDetonationLock': ('L', (255, 255, 255)), 'MFS:prop_vehicle_jeep': ('B', (0, 255, 0)),
}


def floors(root: Path, zmin: float, zmax: float):
    lvl = json.loads((root / 'level.json').read_text())
    for w in lvl['world']:
        # collision chunks hold the walkable surfaces (no decals / translucent sheets)
        if not w['mesh'].split('/')[-1].startswith('Collision_'):
            continue
        for tri, n in load_tris(root, w['mesh'], w['location']):
            if n[2] > 0.7 and zmin <= sum(p[2] for p in tri) / 3 <= zmax:
                yield tri


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('out')
    ap.add_argument('--px-per-m', type=float, default=3.0)
    ap.add_argument('--zmin', type=float, default=-1e9)
    ap.add_argument('--zmax', type=float, default=1e9)
    ap.add_argument('--grid', type=float, default=10.0, help='grid spacing in metres (0 = off)')
    a = ap.parse_args()
    maps = [Path(a.a), Path(a.b)]
    tris = [list(floors(m, a.zmin, a.zmax)) for m in maps]
    pts = [p for t in tris for tri in t for p in tri]
    x0, x1 = min(p[0] for p in pts), max(p[0] for p in pts)
    y0, y1 = min(p[1] for p in pts), max(p[1] for p in pts)
    k = a.px_per_m / 100.0
    W, H = int((x1 - x0) * k) + 40, int((y1 - y0) * k) + 40

    # Image: +X right, +Y down (UE top view with X east, Y south)
    def px(p):
        return (20 + (p[0] - x0) * k, 20 + (p[1] - y0) * k)

    layers = []
    for t, col in zip(tris, ((60, 120, 255), (255, 140, 30))):
        img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        for tri in t:
            d.polygon([px(p) for p in tri], fill=col + (120,))
        layers.append(img)
    out = Image.new('RGBA', (W, H), (10, 10, 14, 255))
    for l in layers:
        out = Image.alpha_composite(out, l)
    d = ImageDraw.Draw(out)
    if a.grid > 0:
        step = a.grid * 100.0
        gx = x0 - x0 % step
        while gx < x1:
            d.line([px((gx, y0)), px((gx, y1))], fill=(255, 255, 255, 25))
            gx += step
        gy = y0 - y0 % step
        while gy < y1:
            d.line([px((x0, gy)), px((x1, gy))], fill=(255, 255, 255, 25))
            gy += step
    for m, edge in zip(maps, ((120, 170, 255), (255, 180, 80))):
        lvl = json.loads((m / 'level.json').read_text())
        for s in lvl['spawns']:
            x, y = px(s['location'])
            if s.get('class', '').endswith('MFPlayerStart'):
                team = s['props'].get('TeamNumber')
                c = {'0': (80, 140, 255), '1': (255, 70, 60)}.get(team, (200, 200, 200))
                d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=c, outline=edge)
            elif s.get('mf_class') in MARK:
                ch, c = MARK[s['mf_class']]
                d.rectangle([x - 6, y - 6, x + 6, y + 6], outline=edge, width=2)
                d.text((x - 3, y - 6), ch, fill=c)
    d.text((10, 4), f'blue: {maps[0].name}   orange: {maps[1].name}   grid {a.grid:g} m', fill=(255, 255, 255))
    out.convert('RGB').save(a.out)
    print(f'{a.out}: {W}x{H}')


if __name__ == '__main__':
    main()
