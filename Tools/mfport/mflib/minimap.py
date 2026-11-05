"""Minimaps: MF's tiled MiniMapInfo pictures stitched into one texture, and a generated one for maps
that never had a MiniMapInfo (community and VMF maps).

The HUD (ADFHUD::DrawMiniMapMF) reads one texture over the map_info record's bounds at MF's scale,
64 uu (144 cm) a pixel; black is see-through (lumalpha), as MF draws its map translucent.
"""
from __future__ import annotations

import json
import math
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

from .actors import rotate
from .config import Config
from .textures import load_texture, write_manifest

CM_PER_PIXEL = 144.0          # MiniMapInfo.MapUnitsToPixels 64 uu
OUTSIDE, FLOOR_LOW, FLOOR_HIGH, EDGE = 60, 120, 205, 85   # grey levels like the MF pictures
STEP_CM = 250.0               # a step between pixels steeper than 60 degrees draws a line (walls, ledges)


def save(cfg: Config, img: Image.Image, key: str) -> None:
    """Write a minimap picture (grey, black = see-through) and its textures.json entry."""
    grey = img.convert('L')
    out = Image.merge('RGBA', (grey, grey, grey, grey.point(lambda v: min(255, v * 2))))
    dst = cfg.out_dir / 'Textures' / Path(*key.split('.')).with_suffix('.png')
    dst.parent.mkdir(parents=True, exist_ok=True)
    out.save(dst, optimize=True)
    write_manifest(cfg, {key: {'file': str(dst.relative_to(cfg.out_dir)), 'usize': out.width, 'vsize': out.height,
                               'alpha': True, 'override': False, 'ui': True}})


def stitch(cfg: Config, tiles: list[str | None], wide: int, high: int, size: tuple[int, int], key: str) -> None:
    """MiniMapInfo.DrawMap's picture as one texture: MiniMapTex[i * NumTexturesHigh + j] is column
    i, row j, each showing up to 255 of its pixels; TexWidth x TexHeight in all."""
    width, height = size
    out = Image.new('RGB', (width, height))
    x = 0
    for i in range(wide):
        tw, y = min(255, width - x), 0
        for j in range(high):
            th, k = min(255, height - y), i * high + j
            img = load_texture(cfg, tiles[k]) if k < len(tiles) and tiles[k] else None
            if img is not None and tw > 0 and th > 0:
                out.paste(img.convert('RGB').crop((0, 0, tw, th)), (x, y))
            y += th
        x += tw
    save(cfg, out, key)


def _mesh_triangles(root: Path, rel: str):
    meta = json.loads((root / f'{rel}.mfmesh.json').read_text())
    data = (root / rel).parent.joinpath(meta['bin']).read_bytes()
    stride = 11 if meta.get('vertex_layout', '').endswith('col3f') else 8
    n = meta['vertex_count']
    pos = [struct.unpack_from('<3f', data, i * stride * 4) for i in range(n)]
    idx = struct.unpack_from(f"<{meta['index_count']}I", data, n * stride * 4)
    for t in range(0, len(idx), 3):
        yield pos[idx[t]], pos[idx[t + 1]], pos[idx[t + 2]]


def _world_triangles(root: Path, level: dict):
    """Every drawn surface in world cm: the world's chunks (not sky) and the placed static meshes."""
    for w in level['world']:
        if w.get('render') and not w.get('sky'):
            o = w['location']
            for tri in _mesh_triangles(root, w['mesh']):
                yield [(v[0] + o[0], v[1] + o[1], v[2] + o[2]) for v in tri]
    for s in level['spawns']:
        if s['type'] != 'static_mesh':
            continue
        o, rot, sc = s['location'], s.get('rotation', {}), s.get('scale', [1.0, 1.0, 1.0])
        for tri in _mesh_triangles(root, s['mesh']):
            out = []
            for v in tri:
                r = rotate((v[0] * sc[0], v[1] * sc[1], v[2] * sc[2]), rot)
                out.append((r[0] + o[0], r[1] + o[1], r[2] + o[2]))
            yield out


def _play_area(level: dict) -> tuple[float, float, float, float, float, float] | None:
    """Bounds of where play happens (starts, loadout zones, items, generators) plus a margin, and
    the heights around them: a map's skybox room, far scenery and lids above stay off the picture."""
    pts = [s['location'] for s in level['spawns'] if s.get('location') and (
        'PlayerStart' in str(s.get('class', '')) or s['type'] == 'loadout_zone'
        or (s['type'] == 'legacy' and not any(w in s.get('mf_class', '') for w in ('Info', 'Zone', 'Light', 'Sound'))))]
    if len(pts) < 2:
        return None
    xs, ys, zs = [p[0] for p in pts], [p[1] for p in pts], [p[2] for p in pts]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    mx, my = max(3000.0, (x1 - x0) * 0.15), max(3000.0, (y1 - y0) * 0.15)
    return x0 - mx, y0 - my, x1 + mx, y1 + my, min(zs) - 3000.0, max(zs) + 2500.0


def generate(cfg: Config, root: Path, level: dict, key: str) -> list[float] | None:
    """A top-down picture of the map's floors, lighter higher up, lines where the floor steps
    (walls, ledges). Returns the bounds (cm: left, top, right, bottom), None without floors."""
    area = _play_area(level)
    if not area:
        return None
    ax0, ay0, ax1, ay1, az0, az1 = area
    floors = []
    for tri in _world_triangles(root, level):
        t = np.array(tri, dtype=np.float64)
        n = np.cross(t[1] - t[0], t[2] - t[0])
        ln = np.linalg.norm(n)
        if ln <= 0 or abs(n[2]) / ln < 0.3:
            continue   # walls and steep slopes: they show as the steps around them
        if t[:, 0].max() < ax0 or t[:, 0].min() > ax1 or t[:, 1].max() < ay0 or t[:, 1].min() > ay1:
            continue
        if t[:, 2].min() > az1 or t[:, 2].max() < az0:
            continue
        floors.append(t)
    if not floors:
        return None
    allv = np.concatenate(floors)
    fx0, fy0 = max(ax0, allv[:, 0].min()), max(ay0, allv[:, 1].min())
    fx1, fy1 = min(ax1, allv[:, 0].max()), min(ay1, allv[:, 1].max())
    w, h = max(1, math.ceil((fx1 - fx0) / CM_PER_PIXEL)), max(1, math.ceil((fy1 - fy0) / CM_PER_PIXEL))
    zbuf = np.full((h, w), -np.inf)
    for t in floors:
        px, py = (t[:, 0] - fx0) / CM_PER_PIXEL, (t[:, 1] - fy0) / CM_PER_PIXEL
        xa, xb = max(0, int(math.floor(px.min()))), min(w - 1, int(math.ceil(px.max())))
        ya, yb = max(0, int(math.floor(py.min()))), min(h - 1, int(math.ceil(py.max())))
        if xa > xb or ya > yb:
            continue
        gx, gy = np.meshgrid(np.arange(xa, xb + 1) + 0.5, np.arange(ya, yb + 1) + 0.5)
        d = (py[1] - py[2]) * (px[0] - px[2]) + (px[2] - px[1]) * (py[0] - py[2])
        if abs(d) < 1e-9:
            continue
        l0 = ((py[1] - py[2]) * (gx - px[2]) + (px[2] - px[1]) * (gy - py[2])) / d
        l1 = ((py[2] - py[0]) * (gx - px[2]) + (px[0] - px[2]) * (gy - py[2])) / d
        l2 = 1.0 - l0 - l1
        inside = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6)
        if not inside.any():   # smaller than a pixel: its centre's pixel
            cx, cy = min(w - 1, max(0, int(px.mean()))), min(h - 1, max(0, int(py.mean())))
            zbuf[cy, cx] = max(zbuf[cy, cx], t[:, 2].mean())
            continue
        z = l0 * t[0, 2] + l1 * t[1, 2] + l2 * t[2, 2]
        sub = zbuf[ya:yb + 1, xa:xb + 1]
        np.copyto(sub, np.where(inside & (z > sub), z, sub))
    have = np.isfinite(zbuf)
    zs = zbuf[have]
    zlo, zhi = np.percentile(zs, 2), np.percentile(zs, 98)
    k = np.clip((zbuf - zlo) / max(zhi - zlo, 1.0), 0.0, 1.0)
    shade = np.where(have, FLOOR_LOW + (FLOOR_HIGH - FLOOR_LOW) * k, OUTSIDE)
    # lines where the floor steps more than a steep slope would over a pixel
    zf = np.where(have, zbuf, -1e6)
    step = np.zeros_like(have)
    step[:, :-1] |= np.abs(np.diff(zf, axis=1)) > STEP_CM
    step[:-1, :] |= np.abs(np.diff(zf, axis=0)) > STEP_CM
    shade = np.where(step & have, np.minimum(shade, EDGE), shade)
    img = Image.fromarray(shade.astype(np.uint8), 'L').filter(ImageFilter.SMOOTH)
    save(cfg, img, key)
    return [fx0, fy0, fx0 + w * CM_PER_PIXEL, fy0 + h * CM_PER_PIXEL]


def ensure(cfg: Config, root: Path, level: dict, map_name: str) -> None:
    """Give a level without a minimap a generated one (map_info record's 'minimap')."""
    info = next((s for s in level['spawns'] if s['type'] == 'map_info'), None)
    if info is None:   # VMF maps: the HUD finds the minimap on the map's ADFMapInfo
        info = {'type': 'map_info', 'name': 'MapInfo', 'location': [0.0, 0.0, 0.0], 'title': map_name,
                'author': '', 'ideal_players': ''}
        level['spawns'].append(info)
    if info.get('minimap'):
        return
    key = f'{map_name}.MiniMap'
    bounds = generate(cfg, root, level, key)
    if bounds:
        info['minimap'] = {'texture': key, 'bounds': bounds, 'generated': True}
        print(f'  minimap: generated ({round((bounds[2] - bounds[0]) / CM_PER_PIXEL)} x {round((bounds[3] - bounds[1]) / CM_PER_PIXEL)} px)')
