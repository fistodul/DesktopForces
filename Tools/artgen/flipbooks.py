#!/usr/bin/env python3
"""Replacement art for the particle flipbooks of the original game: smoke, fire, blood, bubbles.

Same atlas sizes and grids as the originals (read from work/out/Effects/effects.json), so the
particle systems keep their timing; everything is drawn here, only the tables below are chosen
by eye. Output: Tools/mfport/work/generated/effects/<same path>.png, which `mfport.py effects`
lists as replacement art. Deterministic: the seed is the texture key.

    python3 Tools/artgen/flipbooks.py          # generate every entry
    python3 Tools/artgen/flipbooks.py --sheet  # also write a comparison sheet (scratch)
"""
from __future__ import annotations

import json
import sys
import zlib
from pathlib import Path

import numpy as np
from PIL import Image

from lookalike import MFPORT, fbm

OUT = MFPORT / 'work' / 'generated' / 'effects'


def frame_grid(size: int) -> tuple[np.ndarray, np.ndarray]:
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    return (xx + 0.5) / size * 2 - 1, (yy + 0.5) / size * 2 - 1


def smooth(a: np.ndarray, lo: float, hi: float) -> np.ndarray:
    t = np.clip((a - lo) / (hi - lo), 0, 1)
    return t * t * (3 - 2 * t)


def blob(rng, size: int, radius: float, rough: float) -> np.ndarray:
    """A soft lumpy ball, 0..1."""
    x, y = frame_grid(size)
    r = np.sqrt(x * x + y * y)
    lumps = fbm(rng, size, size, 3.0) - 0.5
    return 1 - smooth(r / (1.45 * radius * (1 + rough * lumps * 1.6)), 0.5, 1.0)


def puff(rng, size, t, o):
    """Smoke: grows, then thins out."""
    grow = o.get('start', 0.25) + (o.get('end', 0.75) - o.get('start', 0.25)) * t ** 0.6
    m = blob(rng, size, grow, o.get('rough', 0.5))
    wisp = 0.55 + 0.9 * fbm(rng, size, size, 2.6)
    fade = np.clip(1.15 - t ** 2.2 * 1.25, 0, 1)
    v = m * wisp * fade
    return np.array(o['color'], np.float32) * v[..., None], np.clip(v * 1.6, 0, 1)


def fire(rng, size, t, o):
    """Burning ball: white hot, then orange, red, gone."""
    grow = o.get('start', 0.2) + (o.get('end', 0.7) - o.get('start', 0.2)) * min(1, t * 2.5)
    m = blob(rng, size, grow, 0.7) * (0.6 + 0.8 * fbm(rng, size, size, 2.6))
    heat = np.clip(m * (1.5 - 1.6 * t), 0, 1)[..., None]
    white, yellow, red = np.array((255, 245, 215.)), np.array((250, 165, 50.)), np.array((150, 30, 5.))
    rgb = np.where(heat > 0.5, yellow + (white - yellow) * (heat - 0.5) * 2, red + (yellow - red) * heat * 2)
    v = np.clip(m * (1.2 - t ** 1.5 * 1.2), 0, 1)
    return rgb * v[..., None], np.clip(v * 1.5, 0, 1)


def spang(rng, size, t, o):
    """Blood hit: a burst that opens up into a thin ring of drops."""
    m = blob(rng, size, 0.35 + 0.5 * t, 0.9)
    specks = smooth(fbm(rng, size, size, 2.2), 0.42, 0.62)
    v = m * (0.35 + 0.65 * specks) * (1 - 0.5 * t)
    return np.array(o['color'], np.float32) * v[..., None], np.clip(v * 1.5, 0, 1)


def bubble(rng, size, t, o):
    x, y = frame_grid(size)
    r = np.sqrt(x * x + y * y)
    ring = smooth(r, 0.6, 0.9) * smooth(1.0 - r, 0.0, 0.08)
    high = smooth(0.5 - np.hypot(x + 0.3, y + 0.42), 0.0, 0.4) * 0.55
    v = np.clip(ring * 0.5 + high, 0, 1)
    return np.array((150, 155, 175.)) * v[..., None], np.clip(v + (r < 0.97) * 0.25, 0, 1)


def glow(rng, size, t, o):
    x, y = frame_grid(size)
    r = np.sqrt(x * x + y * y)
    core = np.exp(-r * r * 22) + 0.3 * np.exp(-r * r * 4)
    a = np.arctan2(y, x)
    star = np.exp(-r * 2.4) * np.clip(np.cos(a * o.get('rays', 3)) ** 40, 0, 1) * 0.5
    v = np.clip(core + star, 0, 1) * smooth(1 - r, 0, 0.25)
    return np.array(o['color'], np.float32) * v[..., None], v


def waterfall(rng, size, t, o):
    """Top rows: mist. Bottom rows: a white sheet of falling water, tearing at the foot."""
    x, y = frame_grid(size)
    if t < 0.5:
        v = blob(rng, size, 0.85, 0.7) * (0.15 + 0.35 * fbm(rng, size, size, 2.2))
    else:
        streaks = fbm(rng, 1, size, 0.9)[0]
        foot = -0.1 + 0.5 * (streaks - 0.5) + 0.35
        v = smooth(foot - y, -0.15, 0.25) * smooth(y + 0.35, 0, 0.5) * (0.4 + 1.0 * streaks)
    v = np.clip(v, 0, 1)
    return np.array((225, 235, 240.)) * v[..., None], v


# flipbook key (lower case): painter, its options
BOOKS = {
    'rageeffects.densesmoke.densesmoke_a01': (puff, {'color': (135, 140, 148), 'start': 0.3, 'end': 0.72}),
    'rageeffects.thicksmoke.thicksmoke_a01': (puff, {'color': (82, 87, 96), 'start': 0.3, 'end': 0.78}),
    'rageeffects.windysmoke.windysmoke_a01': (puff, {'color': (72, 76, 84), 'start': 0.2, 'end': 0.82, 'rough': 0.7}),
    'rageeffects.wispysmoke.wispysmoke_a01': (puff, {'color': (170, 172, 182), 'start': 0.12, 'end': 0.65, 'rough': 1.1}),
    'rageeffects.wispysmoke.wispysmoke_a02': (puff, {'color': (170, 172, 182), 'start': 0.2, 'end': 0.65, 'rough': 1.1}),
    'rageeffects.wispysmoke.wispysmoke_a04': (puff, {'color': (165, 167, 178), 'start': 0.28, 'end': 0.65, 'rough': 1.1}),
    'rageeffects.wispysmoke.wispysmoke_a05': (puff, {'color': (160, 162, 174), 'start': 0.32, 'end': 0.65, 'rough': 1.1}),
    'rageeffects.mushboom.mush_a01': (fire, {'start': 0.15, 'end': 0.45}),
    'rageeffects.oldfire.oldfire_a01': (fire, {'start': 0.3, 'end': 0.8}),
    'rageeffects.windyfire.windyfire_a01': (fire, {'start': 0.08, 'end': 0.55}),
    'rageeffects.bloodspang.spang_a01': (spang, {'color': (150, 15, 10)}),
    'rageeffects.bloodspang.spangblack_a01': (spang, {'color': (25, 25, 35)}),
    'rageeffects.bloodspang.spangsub_a01': (spang, {'color': (15, 190, 190)}),
    'rageeffects.tex_bubble1': (bubble, {}),
    'rageeffects.tex_bubble2': (bubble, {}),
    'rageeffects.tex_bubble3': (bubble, {}),
    'rage_generic.light.generic_lens_1': (glow, {'color': (255, 255, 250), 'rays': 3}),
    'rage_warehouse.object.ware_sparkle_00': (glow, {'color': (255, 170, 60), 'rays': 4}),
    'dam.waterfall_a01': (waterfall, {}),
}


def make(key: str, info: dict, size: tuple[int, int]) -> Image.Image:
    paint, options = BOOKS[key]
    cols, rows = info['cols'], info['rows']
    w, h = size[0] // cols, size[1] // rows
    side = min(w, h)
    rng = np.random.default_rng(zlib.crc32(key.encode()))
    rgba = np.zeros((size[1], size[0], 4), np.float32)
    frames = max(info['frames'], 1)
    for i in range(frames):
        t = i / max(frames - 1, 1)
        rgb, alpha = paint(rng, side, t, options)
        x, y = (i % cols) * w + (w - side) // 2, (i // cols) * h + (h - side) // 2
        rgba[y:y + side, x:x + side, :3] = rgb
        rgba[y:y + side, x:x + side, 3] = alpha * 255
    return Image.fromarray(np.clip(rgba, 0, 255).astype(np.uint8), 'RGBA')


def main(argv: list[str]) -> None:
    books = json.loads((MFPORT / 'work' / 'out' / 'Effects' / 'effects.json').read_text())['flipbooks']
    done = []
    for key in BOOKS:
        info = books.get(key)
        if not info:
            continue
        rel = Path(info['file']).relative_to('Effects')
        with Image.open(MFPORT / 'work' / 'out' / info['file']) as original:
            size = original.size
        img = make(key, info, size)
        dst = OUT / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        img.save(dst, optimize=True)
        done.append((info, img))
    print(f'[flipbooks] {len(done)} atlases -> {OUT}')
    if '--sheet' in argv:
        cell, cols = 170, 7
        out = Image.new('RGB', (cols * cell * 2, (len(done) // cols + 1) * cell))
        for i, (info, img) in enumerate(done):
            x, y = (i % cols) * cell * 2, (i // cols) * cell
            for j, im in enumerate((Image.open(MFPORT / 'work' / 'out' / info['file']), img)):
                bg = Image.new('RGBA', im.size, (0, 0, 0, 255))
                bg.alpha_composite(im.convert('RGBA'))
                out.paste(bg.convert('RGB').resize((cell, cell)), (x + j * cell, y))
        path = Path(__file__).resolve().parent / 'preview' / 'flipbooks_sheet.png'
        path.parent.mkdir(parents=True, exist_ok=True)
        out.save(path)
        print(f'[flipbooks] comparison sheet -> {path}')


if __name__ == '__main__':
    main(sys.argv[1:])
