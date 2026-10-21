#!/usr/bin/env python3
"""The game's own art: procedural textures made here, plus the team's source files.

Everything lands in Tools/artgen/out/<folder>/ and `mfport.py import --steps art` brings it
into /Game/MF/<folder>/ (textures, sounds, and the materials that use them). None of it comes
from the original game, so it is part of the public repository (see CREDITS.md for authors).

  generated  procedural textures below, deterministic (same seed, same image) -> out/Art/
  SourceArt/ files made by the team (menu backgrounds, music, mod icons) -> out/<same path>
             images become PNG (8-bit BMPs: palette index 0 is transparent, the UE1 way),
             audio becomes WAV (mp3 / ogg / flac need ffmpeg)

    python3 Tools/artgen/artgen.py              # all
    python3 Tools/artgen/artgen.py crate        # one generator
    python3 Tools/artgen/artgen.py sourceart    # only the SourceArt conversion

Textures:
  crate   T_MF_Crate (512): olive military crate face, planks in a frame, nails; alpha
          holds the stencil (a chevron emblem + "LOADOUT"), which M_MF_Crate paints in the
          team colour.
  bullethole0-3  Decals/T_MF_BulletHole* (128): a pit, crushed ring, flecks (RGBA)
  scorch0-1      Decals/T_MF_Scorch* (256): a blast mark with a ragged sooty edge (RGBA)
"""
from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = Path(__file__).resolve().parent / 'out'
SOURCE_ART = Path(__file__).resolve().parents[2] / 'SourceArt'


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


def bullet_hole(variant: int, size: int = 128) -> Image.Image:
    """A bullet hole for decals: a dark pit, a ring of crushed material and chipped flecks
    around it; colour darkens the surface, alpha says how much (soft edges)."""
    rng = np.random.default_rng(4200 + variant)
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    c = size / 2 + rng.uniform(-3, 3, 2)
    ang = np.arctan2(y - c[1], x - c[0])
    # a ragged radius: a few sine wobbles plus noise
    wobble = sum(rng.uniform(0.04, 0.12) * np.sin(k * ang + rng.uniform(0, 6.3)) for k in (3, 5, 7))
    r = np.hypot(x - c[0], y - c[1]) / (size * 0.5) * (1.0 + wobble) * rng.uniform(0.9, 1.1)
    noise = _noise(rng, size, 4)
    pit = np.clip((0.3 - r) / 0.08, 0, 1)                       # the hole itself
    ring = np.clip(1 - np.abs(r - 0.4) / 0.14, 0, 1) * (0.5 + 0.5 * noise)
    flecks = (noise > 0.7).astype(np.float32) * np.clip((0.9 - r) / 0.4, 0, 1)
    alpha = np.clip(pit + ring * 0.8 + flecks * 0.5, 0, 1)
    shade = 0.08 + 0.35 * (1 - pit) * (0.6 + 0.4 * noise)   # near black in the pit, grey dust around
    rgb = np.stack([shade * 0.95, shade * 0.92, shade * 0.88], -1)
    img = np.concatenate([rgb * 255, alpha[..., None] * 255], -1)
    return Image.fromarray(img.clip(0, 255).astype(np.uint8), 'RGBA')


def scorch(variant: int, size: int = 256) -> Image.Image:
    """A blast mark for decals: soot fading out from the middle with a ragged, streaky edge."""
    rng = np.random.default_rng(5300 + variant)
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    c = size / 2
    ang = np.arctan2(y - c, x - c)
    rays = sum(rng.uniform(0.01, 0.04) * np.sin(k * ang + rng.uniform(0, 6.3)) for k in (5, 9, 13, 17))
    r = np.hypot(x - c, y - c) / (size * 0.5)
    fbm = _fbm(rng, size)
    edge = 0.8 + rays + (fbm - 0.5) * 0.35
    alpha = np.clip(1 - r / np.maximum(edge, 0.1), 0, 1) ** 0.8 * (0.6 + 0.4 * fbm)
    shade = 0.05 + 0.18 * fbm * np.clip(r * 1.5, 0, 1)        # black core, brownish soot outside
    rgb = np.stack([shade * 1.1, shade * 0.95, shade * 0.8], -1)
    img = np.concatenate([rgb * 255, alpha[..., None] * 255], -1)
    return Image.fromarray(img.clip(0, 255).astype(np.uint8), 'RGBA')


GENERATORS = {
    'crate': ('Art/T_MF_Crate.png', crate),
    # decals: bullet holes and scorch marks (MF's own RagePock / Scorch art stays in the classic pack)
    **{f'bullethole{i}': (f'Art/Decals/T_MF_BulletHole{i}.png', (lambda i=i: bullet_hole(i))) for i in range(4)},
    **{f'scorch{i}': (f'Art/Decals/T_MF_Scorch{i}.png', (lambda i=i: scorch(i))) for i in range(2)},
}


def convert_source_art() -> int:
    """SourceArt/<path>.<ext> -> out/<path>.png | .wav"""
    n = 0
    for src in sorted(SOURCE_ART.rglob('*')):
        if not src.is_file() or src.name.startswith('.') or src.suffix.lower() == '.md':
            continue
        rel = src.relative_to(SOURCE_ART)
        ext = src.suffix.lower()
        if ext in ('.bmp', '.png', '.pcx', '.tga'):
            dst = (OUT / rel).with_suffix('.png')
            dst.parent.mkdir(parents=True, exist_ok=True)
            img = Image.open(src)
            if img.mode in ('P', 'L'):
                # UE1 masked import (#exec ... Flags=2): colour index 0 is see-through; greyscale
                # BMPs (Totem's icon) are 8-bit too, index 0 = black
                mask = img.point(lambda i: 0 if i == 0 else 255, mode='L')
                img = img.convert('RGBA')
                img.putalpha(mask)
            img.save(dst, optimize=True)
        elif ext == '.wav':
            dst = OUT / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(src, dst)
        elif ext in ('.mp3', '.ogg', '.flac'):
            dst = (OUT / rel).with_suffix('.wav')
            dst.parent.mkdir(parents=True, exist_ok=True)
            if not dst.exists() or dst.stat().st_mtime < src.stat().st_mtime:
                subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(src), '-ar', '44100', '-ac', '2', str(dst)], check=True)
        else:
            print(f'[artgen] skipped {rel} (unknown type)')
            continue
        n += 1
    print(f'[artgen] SourceArt: {n} files -> {OUT}')
    return n


def main(names: list[str]) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for name in names or [*GENERATORS, 'sourceart']:
        if name == 'sourceart':
            convert_source_art()
            continue
        file, fn = GENERATORS[name]
        (OUT / file).parent.mkdir(parents=True, exist_ok=True)
        fn().save(OUT / file, optimize=True)
        print(f'[artgen] {name} -> {OUT / file}')


if __name__ == '__main__':
    main(sys.argv[1:])
