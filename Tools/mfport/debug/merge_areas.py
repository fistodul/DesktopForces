"""Per-area comparison of an MF map and its aligned Source remake (Docs/WarehouseMerge.md).

Areas are the original map's zones. For each: render triangles and props of both versions
inside the zone box (a rough "how much detail" measure), plus a side-by-side floor plan
crop. Writes <out>/areas.md and <out>/<zone>.png; the team picks a version per area.

Usage: python3 merge_areas.py mf-warehouse mfs-warehouse work/merge
"""
import argparse
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from floorplan import load_tris  # noqa: E402
from mflib import config  # noqa: E402

MIN_SIZE = 600.0   # cm; smaller zones are doorways / portals


def inside(p, lo, hi, pad=0.0):
    return all(lo[i] - pad <= p[i] <= hi[i] + pad for i in range(3))


def render_tris(root: Path, lvl: dict):
    """(centroid, tri, normal) of every visible world triangle."""
    for w in lvl['world']:
        if w['mesh'].split('/')[-1].startswith('Collision_'):
            continue
        for tri, n in load_tris(root, w['mesh'], w['location']):
            yield tuple(sum(p[i] for p in tri) / 3 for i in range(3)), tri, n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mf')
    ap.add_argument('src')
    ap.add_argument('out')
    a = ap.parse_args()
    cfg = config.load()
    maps = cfg.out_dir / 'Maps'
    roots = [maps / a.mf, maps / a.src]
    lvls = [json.loads((r / 'level.json').read_text()) for r in roots]
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    tris = [list(render_tris(r, l)) for r, l in zip(roots, lvls)]
    # MF static meshes (triangle counts from their .mfmesh) / Source props (counted from the VMF)
    mf_props = []
    for s in lvls[0]['spawns']:
        if s['type'] == 'static_mesh':
            meta = json.loads((roots[0] / f"{s['mesh']}.mfmesh.json").read_text())
            mf_props.append((s['location'], meta['index_count'] // 3))
    src_props = [(s['location'], s['mesh']) for s in lvls[1]['spawns'] if s['type'] == 'static_mesh']
    src_skipped = sum(lvls[1].get('skipped_models', {}).values())

    # the team's picks live in merge/<map>.toml; the table only shows them
    import tomllib
    preset = Path(__file__).resolve().parents[1] / 'merge' / f'{a.mf}.toml'
    picks = tomllib.loads(preset.read_text()).get('zones', {}) if preset.exists() else {}
    rows = []
    names = lvls[0]['zones']
    for z, (lo, hi) in sorted(lvls[0]['zone_bounds'].items(), key=lambda kv: int(kv[0])):
        name = names[int(z)] or f'zone{z}'
        if hi[0] - lo[0] < MIN_SIZE or hi[1] - lo[1] < MIN_SIZE or name.startswith('SkyZone'):
            continue
        counts = [sum(1 for c, _, _ in t if inside(c, lo, hi)) for t in tris]
        props = (sum(n for p, n in mf_props if inside(p, lo, hi)), sum(1 for p, _ in src_props if inside(p, lo, hi, 50)))
        rows.append((name, z, lo, hi, counts, props))
        # side-by-side crop: floors coloured by height, MF left, Source right
        k = 600.0 / max(hi[0] - lo[0], hi[1] - lo[1])
        Wp, Hp = int((hi[0] - lo[0]) * k) + 20, int((hi[1] - lo[1]) * k) + 20
        sheet = Image.new('RGB', (Wp * 2 + 10, Hp + 24), (12, 12, 16))
        for i, t in enumerate(tris):
            img = Image.new('RGB', (Wp, Hp), (20, 20, 26))
            d = ImageDraw.Draw(img)
            for c, tri, n in sorted((x for x in t if inside(x[0], lo, hi)), key=lambda x: x[0][2]):
                if n[2] < 0.7:
                    continue
                h = max(0.0, min(1.0, (c[2] - lo[2]) / max(1.0, hi[2] - lo[2])))
                col = (int(60 + 180 * h), int(90 + 100 * (1 - abs(h - 0.5) * 2)), int(220 - 160 * h))
                d.polygon([(10 + (p[0] - lo[0]) * k, 10 + (p[1] - lo[1]) * k) for p in tri], fill=col)
            sheet.paste(img, (i * (Wp + 10), 24))
        dd = ImageDraw.Draw(sheet)
        dd.text((6, 4), f'{name}: original {counts[0]} tris / {props[0]} prop tris', fill=(200, 200, 255))
        dd.text((Wp + 16, 4), f'remake {counts[1]} tris / {props[1]} props', fill=(255, 200, 150))
        sheet.save(out / f'{name}.png')

    with open(out / 'areas.md', 'w') as f:
        f.write(f'# {a.mf} vs {a.src}, per original zone\n\n')
        f.write(f'Picks ("Use") are read from `Tools/mfport/merge/{a.mf}.toml` - edit them there.\n\n')
        f.write('Bounds in m. "tris" = visible world triangles in the zone box (BSP cuts inflate the\n'
                'original\'s count); MF props = static mesh triangles; remake props = converted MF props\n'
                f'({src_skipped} Valve props are left out map-wide). Shots: textured top views from\n'
                '`merge_shots.py` (shots/<zone>_1000.png).\n\n')
        f.write('| Zone | Bounds (m) | Original tris | Remake tris | MF prop tris | Remake props | Plan | Shot | Use |\n|---|---|---|---|---|---|---|---|---|\n')
        for name, z, lo, hi, c, p in rows:
            b = f'{lo[0]/100:.0f},{lo[1]/100:.0f} – {hi[0]/100:.0f},{hi[1]/100:.0f}'
            shot = f'[shot](shots/{name}_1000.png)' if (out / 'shots' / f'{name}_1000.png').exists() else ''
            f.write(f'| {name} | {b} | {c[0]} | {c[1]} | {p[0]} | {p[1]} | [plan]({name}.png) | {shot} | {picks.get(name, '?')} |\n')
    print(f'{len(rows)} areas -> {out}/areas.md')


if __name__ == '__main__':
    main()
