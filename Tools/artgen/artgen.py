#!/usr/bin/env python3
"""Procedural textures made for this project (no original game art, MPL-2.0 like the code).

Writes PNGs to Tools/artgen/out/; `mfport.py import --steps art` brings them into
/Game/MF/Art/ (textures + the materials that use them). Deterministic: same seed, same image.

    python3 Tools/artgen/artgen.py            # all
    python3 Tools/artgen/artgen.py crate      # one

Textures:
  crate   T_MF_Crate (512): olive military crate face, planks in a frame, nails; alpha
          holds the stencil (a chevron emblem + "LOADOUT"), which M_MF_Crate paints in the
          team colour.
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = Path(__file__).resolve().parent / 'out'


def _noise(rng: np.random.Generator, size: int, scale: int) -> np.ndarray:
    """Smooth value noise in 0..1 (bilinear upscale of a coarse random grid)."""
    coarse = rng.random((size // scale + 2, size // scale + 2)).astype(np.float32)
    img = Image.fromarray((coarse * 255).astype(np.uint8)).resize((size + 2 * scale, size + 2 * scale), Image.BICUBIC)
    return np.asarray(img, dtype=np.float32)[scale:scale + size, scale:scale + size] / 255.0


def _fbm(rng: np.random.Generator, size: int, octaves=((64, 0.5), (16, 0.3), (4, 0.2))) -> np.ndarray:
    out = np.zeros((size, size), np.float32)
    for scale, weight in octaves:
        out += _noise(rng, size, scale) * weight
    return out


def crate(size: int = 512) -> Image.Image:
    rng = np.random.default_rng(1977)
    base = np.array([86, 96, 58], np.float32)          # olive drab
    img = np.tile(base, (size, size, 1))

    # horizontal planks with wood grain (stretched noise) and a dark gap between them
    planks = 5
    frame = size // 12
    grain = _noise(rng, size, 8)
    grain = np.asarray(Image.fromarray((grain * 255).astype(np.uint8)).resize((size, size // 16)).resize((size, size), Image.BICUBIC), np.float32) / 255.0
    ph = (size - 2 * frame) / planks
    for i in range(planks):
        y0, y1 = int(frame + i * ph), int(frame + (i + 1) * ph)
        tint = 0.9 + 0.2 * rng.random()
        img[y0:y1] *= tint
        img[y0:y0 + 3] *= 0.45                           # gap shadow
        img[y1 - 2:y1] *= 1.12                           # lit edge
    img *= (0.82 + 0.3 * grain)[..., None]

    # frame boards around the face (vertical grain), darker
    fgrain = np.asarray(Image.fromarray((_noise(rng, size, 8) * 255).astype(np.uint8)).resize((size // 16, size)).resize((size, size), Image.BICUBIC), np.float32) / 255.0
    mask = np.zeros((size, size), bool)
    mask[:frame] = mask[-frame:] = True
    mask[:, :frame] = mask[:, -frame:] = True
    img[mask] = (base * 0.78)[None] * (0.8 + 0.35 * fgrain[mask])[:, None]
    for e in (frame, size - frame):
        img[e - 2:e + 2, :] *= 0.5
        img[:, e - 2:e + 2] *= 0.5

    # wear: dirt and scratches
    dirt = _fbm(rng, size)
    img *= (0.75 + 0.4 * dirt)[..., None]
    for _ in range(60):
        x, y = rng.integers(0, size, 2)
        length = rng.integers(6, 30)
        ang = rng.random() * np.pi
        xs = np.clip((x + np.cos(ang) * np.arange(length)).astype(int), 0, size - 1)
        ys = np.clip((y + np.sin(ang) * np.arange(length)).astype(int), 0, size - 1)
        img[ys, xs] = img[ys, xs] * 1.1 + 3

    # nails in the frame corners and along the planks
    out = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(out)
    for cx in (frame // 2, size - frame // 2):
        for i in range(planks):
            cy = int(frame + (i + 0.5) * ph)
            d.ellipse((cx - 4, cy - 4, cx + 4, cy + 4), fill=(46, 46, 40))
            d.ellipse((cx - 2, cy - 3, cx + 1, cy), fill=(120, 120, 105))

    # stencil -> alpha: chevrons above "LOADOUT", slightly worn
    sten = Image.new('L', (size, size), 0)
    sd = ImageDraw.Draw(sten)
    cx, top = size // 2, int(size * 0.24)
    w, h, t = size * 0.22, size * 0.12, size * 0.05
    for k in range(2):
        y = top + k * (t * 1.6)
        sd.polygon([(cx - w, y), (cx, y + h), (cx + w, y), (cx + w, y + t), (cx, y + h + t), (cx - w, y + t)], fill=255)
    try:
        font = ImageFont.truetype('DejaVuSans-Bold.ttf', size // 9)
    except OSError:
        font = ImageFont.load_default()
    text = 'LOADOUT'
    tw = sd.textlength(text, font=font)
    sd.text((cx - tw / 2, int(size * 0.6)), text, fill=255, font=font)
    wear = (_fbm(rng, size, ((8, 0.6), (2, 0.4))) > 0.32).astype(np.float32)
    alpha = np.asarray(sten.filter(ImageFilter.GaussianBlur(0.8)), np.float32) * wear
    rgba = out.convert('RGBA')
    rgba.putalpha(Image.fromarray(alpha.astype(np.uint8)))
    return rgba


GENERATORS = {'crate': ('T_MF_Crate.png', crate)}


def main(names: list[str]) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for name in names or GENERATORS:
        file, fn = GENERATORS[name]
        fn().save(OUT / file, optimize=True)
        print(f'[artgen] {name} -> {OUT / file}')


if __name__ == '__main__':
    main(sys.argv[1:])
