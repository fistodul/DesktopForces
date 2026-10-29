#!/usr/bin/env python3
"""Replacement art: new procedural textures that stand in for original Mobile Forces ones.

Core maps (community maps that are part of Desktop Forces, Tools/mfport/packs.toml) still use
some textures of the original game. This makes our own versions: each entry names a procedural
generator (bricks, planks, corrugated metal, plaster ...) and its layout; only the original's
*colours* are sampled (dark / mid / light tones), never its pixels.

  lookalikes.toml       hand-tuned entries
  lookalikes_auto.toml  written by --auto for every other texture core maps still borrow: the
                        generator picked from the name and the stripes / grid found in the
                        original, colours and size stored, so it regenerates without the game

Output: Tools/mfport/work/generated/textures/<package>/<group>/<name>.png (git-ignored,
re-created by this script), which the pipeline takes as replacement art after any hand-made
file in Tools/mfport/overrides/textures (committed, wins).

    python3 Tools/artgen/lookalike.py --auto       # (re)classify what core maps need
    python3 Tools/artgen/lookalike.py              # generate every entry
    python3 Tools/artgen/lookalike.py ap_road3     # entries whose key contains the text
    python3 Tools/artgen/lookalike.py --sheet      # also write a comparison sheet (scratch)

Generated art is ours (MPL-2.0 like the code). Deterministic: the seed is the texture key.
"""
from __future__ import annotations

import json
import sys
import tomllib
import zlib
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
TABLE = HERE / 'lookalikes.toml'
AUTO_TABLE = HERE / 'lookalikes_auto.toml'
MFPORT = HERE.parent / 'mfport'
OUT = MFPORT / 'work' / 'generated' / 'textures'


# --- noise ---------------------------------------------------------------------------------

def fbm(rng: np.random.Generator, h: int, w: int, beta: float = 1.8, lo: float = 0.0) -> np.ndarray:
    """Tileable 1/f^beta noise in 0..1 (FFT filtered white noise: periodic by construction).
    lo cuts the largest features (0 = keep everything)."""
    white = rng.standard_normal((h, w))
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    f = np.sqrt(fx * fx + fy * fy)
    f[0, 0] = 1.0
    amp = f ** (-beta / 2.0)
    amp[0, 0] = 0.0
    if lo > 0:
        amp *= 1.0 - np.exp(-(f / lo) ** 2)
    out = np.real(np.fft.ifft2(np.fft.fft2(white) * amp))
    out -= out.min()
    return out / (out.max() or 1.0)


def stretched(rng, h, w, sx: float, sy: float, beta=1.6) -> np.ndarray:
    """Noise stretched along x (sx > 1) or y: wood grain, brushed metal, streaks (tileable)."""
    hh, ww = max(4, int(h / sy)), max(4, int(w / sx))
    n = fbm(rng, hh, ww, beta)
    img = Image.fromarray((n * 255).astype(np.uint8))
    tiled = Image.new('L', (ww * 3, hh * 3))
    for i in range(3):
        for j in range(3):
            tiled.paste(img, (i * ww, j * hh))
    big = tiled.resize((w * 3, h * 3), Image.BICUBIC)
    return np.asarray(big, np.float32)[h:2 * h, w:2 * w] / 255.0


def speckle(rng, h, w, density: float, size: float = 1.0) -> np.ndarray:
    """Sparse spots (stains, pits) covering about `density` of the surface, 0..1, tileable."""
    n = blur(rng.random((h, w)).astype(np.float32), max(0.6, size * 0.8))
    thr = np.quantile(n, 1 - density)
    return np.clip((n - thr) / max(n.max() - thr, 1e-6), 0, 1) ** 0.5


def blur(a: np.ndarray, radius: float) -> np.ndarray:
    img = Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8))
    # wrap: blur a 3x3 tiling so edges stay seamless
    h, w = a.shape
    t = Image.new('L', (w * 3, h * 3))
    for i in range(3):
        for j in range(3):
            t.paste(img, (i * w, j * h))
    t = t.filter(ImageFilter.GaussianBlur(radius))
    return np.asarray(t, np.float32)[h:2 * h, w:2 * w] / 255.0


# --- colour ---------------------------------------------------------------------------------

def tones(src: Image.Image | None, fallback=((60, 60, 60), (128, 128, 128), (200, 200, 200))):
    """Dark / mid / light colours of the original (10th / 50th / 90th luminance percentile,
    averaged around each): the only thing taken from it."""
    if src is None:
        return [np.array(c, np.float32) for c in fallback]
    a = np.asarray(src.convert('RGB').resize((64, 64)), np.float32).reshape(-1, 3)
    lum = a @ np.array([0.299, 0.587, 0.114], np.float32)
    order = np.argsort(lum)
    n = len(order)
    out = []
    for p in (0.1, 0.5, 0.9):
        i = int(p * (n - 1))
        sel = order[max(0, i - n // 20): min(n, i + n // 20 + 1)]
        out.append(a[sel].mean(axis=0))
    return out


def ramp(t: np.ndarray, dark, mid, light) -> np.ndarray:
    """t 0..1 -> colour through dark, mid, light."""
    t = np.clip(t, 0, 1)[..., None]
    lo = dark + (mid - dark) * np.clip(t * 2, 0, 1)
    hi = mid + (light - mid) * np.clip(t * 2 - 1, 0, 1)
    return np.where(t < 0.5, lo, hi)


def shade(rgb: np.ndarray, f) -> np.ndarray:
    f = np.asarray(f, np.float32)
    return rgb * (f[..., None] if f.ndim == 2 else f)


def rgb_from(c) -> np.ndarray:
    return np.array(c, np.float32)


# --- generators -----------------------------------------------------------------------------
# each: (rng, h, w, (dark, mid, light), options) -> float RGB array (h, w, 3), 0..255

def g_noise(rng, h, w, t, o):
    """Concrete, plaster, dirt, painted metal, asphalt: layered noise + spots + streaks."""
    dark, mid, light = t
    n = fbm(rng, h, w, o.get('beta', 1.9)) * 0.7 + fbm(rng, h, w, 1.0) * 0.3
    c = o.get('contrast', 1.0) * 1.3
    v = 0.5 + (n - 0.5) * c
    rgb = ramp(v, dark, mid, light)
    if o.get('spots', 0):
        rgb = shade(rgb, 1 - 0.7 * speckle(rng, h, w, o["spots"], o.get("spot_size", 2)))
    if o.get('streaks', 0):
        s = stretched(rng, h, w, 1, 12)
        rgb = shade(rgb, 1 - o['streaks'] * np.clip(s - 0.45, 0, 1))
    if o.get('stains', 0):
        st = blur(fbm(rng, h, w, 2.6), 3)
        rgb = shade(rgb, 1 - o['stains'] * np.clip(st - 0.55, 0, 1) * 2)
    if o.get('band'):
        # a strip of another colour along the bottom (dirt / grass at a wall's foot)
        b = o['band']
        y0 = int(h * (1 - b.get('height', 0.15)))
        col = rgb_from(b['color'])
        edge = (np.arange(h) - y0)[:, None] + (fbm(rng, 1, w, 1.2)[0] - 0.5)[None, :] * h * 0.04
        m = np.clip(edge / 3.0, 0, 1)
        bandtex = shade(np.broadcast_to(col, (h, w, 3)).copy(), 0.8 + 0.4 * fbm(rng, h, w, 1.4))
        rgb = rgb * (1 - m[..., None]) + bandtex * m[..., None]
    return rgb


def g_bricks(rng, h, w, t, o):
    """Bricks / blocks with mortar, running bond."""
    dark, mid, light = t
    rows, cols = o.get('rows', 8), o.get('cols', 4)
    mortar = o.get('mortar', 0.06)
    mortar_col = rgb_from(o['mortar_color']) if 'mortar_color' in o else (dark * 0.6 + light * 0.4)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    ry = yy / h * rows
    row = np.floor(ry).astype(int)
    rx = xx / w * cols + (row % 2) * 0.5 * o.get('offset', 1.0)
    col = np.floor(rx).astype(int)
    fy, fx = ry - row, rx - col
    tint = rng.uniform(0.82, 1.15, (rows + 1, cols * 2 + 2))
    base = ramp(0.35 + 0.4 * fbm(rng, h, w, 1.5), dark, mid, light)
    base = shade(base, tint[row % rows, col % (cols * 2)])
    my = mortar
    mx = mortar * (h / rows) / (w / cols)          # same width in pixels as the bed joints
    edge = np.minimum(np.minimum(fx, 1 - fx) / mx, np.minimum(fy, 1 - fy) / my)
    m = np.clip(edge - 0.5, 0, 1)
    bevel = np.clip(edge / 3, 0.75, 1.0)
    rgb = shade(base, bevel) * m[..., None] + mortar_col * (1 - m[..., None])
    return shade(rgb, 0.9 + 0.2 * fbm(rng, h, w, 1.0))


def g_tiles(rng, h, w, t, o):
    """Floor tiles / slabs, optionally diamond (45 deg) layout."""
    dark, mid, light = t
    n = o.get('n', 4)
    grout = o.get('grout', 0.05)
    grout_col = rgb_from(o['grout_color']) if 'grout_color' in o else dark * 0.8
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    u, v = xx / w * n, yy / h * n
    if o.get('diamond'):
        u, v = (u + v) / 1.0, (u - v) / 1.0
    iu, iv = np.floor(u).astype(int), np.floor(v).astype(int)
    fu, fv = u - iu, v - iv
    tint = rng.uniform(0.9, 1.1, (64, 64))
    base = ramp(0.4 + 0.35 * fbm(rng, h, w, 1.7), dark, mid, light)
    base = shade(base, tint[iu % 64, iv % 64])
    edge = np.minimum(np.minimum(fu, 1 - fu), np.minimum(fv, 1 - fv)) / grout
    m = np.clip(edge - 0.5, 0, 1)
    bevel = 0.85 + 0.15 * np.clip(edge / 4, 0, 1)
    return shade(base, bevel) * m[..., None] + grout_col * (1 - m[..., None])


def g_planks(rng, h, w, t, o):
    """Boards with grain, gaps and nails; vertical with vertical=true; optional frame."""
    dark, mid, light = t
    n = o.get('n', 5)
    vertical = o.get('vertical', False)
    H, W = (w, h) if vertical else (h, w)
    grain = stretched(rng, H, W, 10, 1, 1.4)
    rings = 0.5 + 0.5 * np.sin(grain * o.get('rings', 18))
    yy = np.mgrid[0:H, 0:W][0].astype(np.float32)
    p = yy / H * n
    idx = np.floor(p).astype(int)
    fp = p - idx
    tint = rng.uniform(0.8, 1.15, n + 1)
    v = 0.2 + 0.5 * grain + 0.25 * rings
    fine = stretched(rng, H, W, 24, 1, 0.9)               # fibres along the board
    rgb = ramp(v + 0.25 * (fine - 0.5), dark, mid, light)
    rgb = shade(rgb, tint[idx % n])
    gap = o.get('gap', 0.04)
    rgb = shade(rgb, np.where(fp < gap, 0.35, np.where(fp < gap * 2.5, 0.8, 1.0)))
    if o.get('weathered', 0):
        rgb = shade(rgb, 1 - o['weathered'] * speckle(rng, H, W, 0.25, 1.5))
    if o.get('nails'):
        img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
        d = ImageDraw.Draw(img)
        for i in range(n):
            y = int((i + 0.5) / n * H)
            for x in (W * 0.06, W * 0.94):
                r = max(1, W // 128)
                d.ellipse((x - r, y - r, x + r, y + r), fill=tuple(int(c) for c in dark * 0.5))
        rgb = np.asarray(img, np.float32)
    if vertical:
        rgb = np.transpose(rgb, (1, 0, 2))
    if o.get('frame'):
        rgb = frame(rgb, rng, o['frame'], dark, mid)
    return rgb


def frame(rgb, rng, width, dark, mid):
    """Darker border boards around a face (crates, framed panels)."""
    h, w, _ = rgb.shape
    fw = int(w * width)
    out = rgb.copy()
    m = np.zeros((h, w), bool)
    m[:fw] = m[-fw:] = True
    m[:, :fw] = m[:, -fw:] = True
    bord = ramp(0.3 + 0.4 * stretched(rng, h, w, 1, 10), dark * 0.8, mid * 0.85, mid)
    out[m] = bord[m]
    out[fw:fw + 2, fw:-fw] *= 0.5
    out[h - fw - 2:h - fw, fw:-fw] *= 1.15
    return out


def g_plywood(rng, h, w, t, o):
    """One sheet: soft grain bands, darker frame (crates)."""
    dark, mid, light = t
    grain = stretched(rng, h, w, 6, 1, 1.3)
    rgb = ramp(0.35 + 0.35 * grain + 0.15 * fbm(rng, h, w, 2.0), dark, mid, light)
    if o.get('frame', 0.07):
        rgb = frame(rgb, rng, o.get('frame', 0.07), dark, mid)
    if o.get('emblem'):
        rgb = emblem(rgb, o['emblem'])
    return rgb


def emblem(rgb, spec):
    """Our own stencil mark (chevrons in a ring), never the original logos."""
    h, w, _ = rgb.shape
    img = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(img)
    cx, cy, r = w / 2, h / 2, min(w, h) * spec.get('size', 0.22)
    d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=255, width=max(2, int(r * 0.16)))
    for k in range(2):
        y = cy - r * 0.35 + k * r * 0.5
        d.line([(cx - r * 0.55, y), (cx, y + r * 0.35), (cx + r * 0.55, y)], fill=255, width=max(2, int(r * 0.18)))
    m = np.asarray(img.filter(ImageFilter.GaussianBlur(0.8)), np.float32)[..., None] / 255.0 * spec.get('opacity', 0.85)
    col = rgb_from(spec.get('color', (190, 60, 30)))
    return rgb * (1 - m) + col * m


def g_corrugated(rng, h, w, t, o):
    """Corrugated sheet: ridges shaded like a sine profile; rust; seams."""
    dark, mid, light = t
    n = o.get('ridges', 12)
    vertical = o.get('vertical', True)
    H, W = (h, w) if vertical else (w, h)
    x = np.arange(W, dtype=np.float32) / W * n * 2 * np.pi
    prof = 0.5 + 0.5 * np.cos(x)
    lit = np.broadcast_to(prof[None, :], (H, W))
    base = ramp(0.3 + 0.45 * lit + 0.1 * stretched(rng, H, W, 1, 8), dark, mid, light)
    base = shade(base, 0.7 + 0.4 * lit)
    if o.get('rust', 0):
        r = np.clip(fbm(rng, H, W, 2.2) - (1 - o['rust']), 0, 1) / max(o['rust'], 1e-3)
        rust = rgb_from(o.get('rust_color', (120, 60, 30)))
        base = base * (1 - r[..., None]) + shade(np.broadcast_to(rust, (H, W, 3)).copy(), 0.7 + 0.5 * lit) * r[..., None]
    if o.get('seam'):
        y = int(H * o['seam'])
        base[y:y + max(2, H // 64)] *= 0.55
    if not vertical:
        base = np.transpose(base, (1, 0, 2))
    return base


def g_siding(rng, h, w, t, o):
    """Overlapping boards / cladding: each board darker at the top under the one above."""
    dark, mid, light = t
    n = o.get('n', 6)
    vertical = o.get('vertical', False)
    H, W = (w, h) if vertical else (h, w)
    y = np.arange(H, dtype=np.float32) / H * n
    f = y - np.floor(y)
    prof = 0.55 + 0.45 * f
    prof = np.where(f < 0.06, 0.35, prof)
    base = ramp(0.3 + 0.5 * prof[:, None] + 0.12 * stretched(rng, H, W, 8, 1), dark, mid, light)
    base = shade(base, np.broadcast_to((0.62 + 0.45 * prof)[:, None], (H, W)))   # structure even in flat colours
    if vertical:
        base = np.transpose(base, (1, 0, 2))
    return base


def g_treadplate(rng, h, w, t, o):
    """Diamond tread plate: raised lozenges in alternating directions on dark steel."""
    dark, mid, light = t
    n = o.get('n', 8)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    u, v = xx / w * n, yy / h * n
    iu, iv = np.floor(u).astype(int), np.floor(v).astype(int)
    fu, fv = u - iu - 0.5, v - iv - 0.5
    flip = ((iu + iv) % 2) * 2 - 1
    a, b = (fu + flip * fv) / 1.414, (fu - flip * fv) / 1.414
    bump = np.clip(1 - (a / 0.38) ** 2 - (b / 0.09) ** 2, 0, 1)
    base = ramp(0.25 + 0.2 * fbm(rng, h, w, 1.6), dark, mid, light)
    hl = np.clip(bump * 1.4, 0, 1)
    return base * (1 - hl[..., None] * 0.6) + light * (hl[..., None] * 0.6)


def g_grate(rng, h, w, t, o):
    """Woven wire mesh (diagonal) over a dark gap (opaque: the holes are drawn dark)."""
    dark, mid, light = t
    n = o.get('n', 10)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    u, v = (xx + yy) / w * n, (xx - yy) / w * n
    wire1 = np.abs((u - np.floor(u)) - 0.5) < o.get('wire', 0.17)
    wire2 = np.abs((v - np.floor(v)) - 0.5) < o.get('wire', 0.17)
    over = (np.floor(u) + np.floor(v)) % 2 == 0
    lit1 = 1 - np.abs((u - np.floor(u)) - 0.5) * 2.5
    lit2 = 1 - np.abs((v - np.floor(v)) - 0.5) * 2.5
    hole = ramp(0.1 + 0.1 * fbm(rng, h, w, 1.5), dark * 0.4, dark, mid * 0.6)
    w1 = ramp(0.4 + 0.5 * lit1, dark, mid, light)
    w2 = ramp(0.4 + 0.5 * lit2, dark, mid, light)
    rgb = hole.copy()
    rgb[wire2] = w2[wire2]
    sel = wire1 & (~wire2 | over)
    rgb[sel] = w1[sel]
    if o.get('masked'):
        return rgb, ((wire1 | wire2) * 255).astype(np.float32)   # see-through between the wires
    return rgb


def g_panel(rng, h, w, t, o):
    """Metal panel: plate with a raised border, rivets, scratches; beam = horizontal flanges."""
    dark, mid, light = t
    base = ramp(0.4 + 0.25 * fbm(rng, h, w, 1.8) + 0.1 * stretched(rng, h, w, 10, 1), dark, mid, light)
    if o.get('rust', 0):
        r = np.clip(fbm(rng, h, w, 2.3) - (1 - o['rust']), 0, 1) / max(o['rust'], 1e-3)
        rust = rgb_from(o.get('rust_color', (110, 60, 30)))
        base = base * (1 - r[..., None]) + rust * r[..., None]
    b = o.get('border', 0.06)
    if b:
        bw = int(min(h, w) * b)
        m = np.zeros((h, w), np.float32)
        m[:bw] = m[-bw:] = 1
        m[:, :bw] = m[:, -bw:] = 1
        base = shade(base, 1 + 0.18 * m)
        base[bw:bw + 2, bw:-bw] *= 0.6
        base[bw:-bw, bw:bw + 2] *= 0.6
    if o.get('flanges'):
        fh = int(h * o['flanges'])
        base[:fh] = shade(base[:fh], 1.2)
        base[-fh:] = shade(base[-fh:], 0.8)
        base[fh:fh + 2] *= 0.5
        base[h - fh - 2:h - fh] *= 1.3
    if o.get('rivets'):
        img = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))
        d = ImageDraw.Draw(img)
        r = max(2, w // 64)
        pts = o['rivets']
        cols = pts.get('cols', [0.04, 0.96])
        nrow = pts.get('rows', 8)
        for x in cols:
            for i in range(nrow):
                cx, cy = x * w, (i + 0.5) / nrow * h
                d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=tuple(int(c) for c in light * 0.9))
                d.ellipse((cx - r + 1, cy, cx + r, cy + r), fill=tuple(int(c) for c in dark * 0.7))
        base = np.asarray(img, np.float32)
    if o.get('bolt'):
        img = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))
        d = ImageDraw.Draw(img)
        cx, cy, r = w / 2, h / 2, w * 0.03
        d.ellipse((cx - r, cy - r * 1.6, cx + r, cy + r * 1.6), fill=tuple(int(c) for c in dark * 0.5))
        base = np.asarray(img, np.float32)
    return base


def g_pipes(rng, h, w, t, o):
    """Row of vertical pipes, cylinder shaded, dark between."""
    dark, mid, light = t
    n = o.get('n', 8)
    x = np.arange(w, dtype=np.float32) / w * n
    f = x - np.floor(x)
    prof = np.sqrt(np.clip(1 - ((f - 0.5) / 0.42) ** 2, 0, 1))
    lit = np.broadcast_to(prof[None, :], (h, w))
    base = ramp(0.15 + 0.8 * lit, dark * 0.5, mid, light)
    return shade(base, 0.9 + 0.15 * stretched(rng, h, w, 1, 12))


def g_lamp(rng, h, w, t, o):
    """Light fittings: round, grid (caged) or tube (fluorescent)."""
    dark, mid, light = t
    kind = o.get('kind', 'round')
    glow = rgb_from(o.get('glow', (255, 250, 225)))
    housing = rgb_from(o.get('housing', mid))
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rgb = np.broadcast_to(rgb_from(o.get('back', dark * 0.4)), (h, w, 3)).copy()
    if kind == 'round':
        r = np.hypot(xx / w - 0.5, yy / h - 0.5)
        ring = (r < 0.47) & (r > 0.36)
        rgb[ring] = housing * (0.8 + 0.4 * (1 - (r[ring] - 0.36) / 0.11))[..., None]
        inner = r <= 0.36
        rgb[inner] = (glow * (0.75 + 0.25 * (1 - r[inner] / 0.36))[..., None])
    elif kind == 'grid':
        inner = (np.abs(xx / w - 0.5) < 0.42) & (np.abs(yy / h - 0.5) < 0.42)
        r = np.hypot(xx / w - 0.5, yy / h - 0.5)
        rgb[inner] = glow * np.clip(1.2 - r[inner] * 1.5, 0.55, 1)[..., None]
        bars = (((xx / w * 6) % 1) < 0.12) | (((yy / h * 6) % 1) < 0.12)
        rgb[inner & bars] = housing * 0.7
        edge = ~inner & (np.abs(xx / w - 0.5) < 0.48) & (np.abs(yy / h - 0.5) < 0.48)
        rgb[edge] = housing
    else:   # tubes
        n = o.get('n', 2)
        rgb[:] = housing * 0.35
        for i in range(n):
            cy = (i + 0.5) / n
            d = np.abs(yy / h - cy)
            tube = (d < 0.13) & (np.abs(xx / w - 0.5) < 0.44)
            rgb[tube] = glow * (1 - d[tube] * 2.5)[..., None]
            halo = (d < 0.22) & ~tube & (np.abs(xx / w - 0.5) < 0.47)
            rgb[halo] = housing * 0.35 + glow * 0.25
    return rgb


SIGN_WORDS = ['DEPOT', 'CARGO', 'ZONE 3', 'NO ENTRY', 'EXIT', 'STORE', 'GARAGE', 'OFFICE', 'DOCK 2',
              'SECTOR 7', 'DANGER', 'KEEP OUT', 'LOADING', 'PARTS', 'SERVICE', 'AUTO', 'MARKET', 'HOTEL',
              'CAFE', 'POWER', 'FUEL', 'STAFF ONLY', 'BAY 4', 'TOOLS', 'DESKTOP FORCES', 'RADIO', 'CLINIC']


def g_sign(rng, h, w, t, o):
    """Our own sign: an emblem, words of our own, or hazard stripes on the sign's colour."""
    from PIL import ImageFont
    col = rgb_from(o.get('color', t[2]))
    mark = tuple(int(c) for c in o.get('mark', (255, 255, 255)))
    rgb = shade(np.broadcast_to(col, (h, w, 3)).copy(), 0.95 + 0.08 * fbm(rng, h, w, 1.2))
    style = o.get('style') or ['emblem', 'words', 'words', 'stripes'][int(rng.integers(0, 4))]
    if style == 'emblem':
        return emblem(rgb, {'size': 0.32, 'color': mark, 'opacity': 1.0})
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
    if style == 'stripes':
        step = max(8, min(w, h) // 5)
        for x in range(-h, w + h, step * 2):
            d.polygon([(x, h), (x + step, h), (x + step + h, 0), (x + h, 0)], fill=mark)
        return np.asarray(img, np.float32)
    border = max(2, min(w, h) // 20)
    d.rectangle((border, border, w - 1 - border, h - 1 - border), outline=mark, width=border)
    words = o.get('words') or SIGN_WORDS[int(rng.integers(0, len(SIGN_WORDS)))]
    size = max(8, int(h * 0.42))
    font = ImageFont.load_default(size=size)
    box = d.textbbox((0, 0), words, font=font)
    while size > 8 and box[2] - box[0] > (w - 4 * border) * 0.9:   # shrink to fit inside the border
        size = int(size * 0.85)
        font = ImageFont.load_default(size=size)
        box = d.textbbox((0, 0), words, font=font)
    d.text(((w - (box[2] - box[0])) / 2 - box[0], (h - (box[3] - box[1])) / 2 - box[1]), words, font=font, fill=mark)
    return np.asarray(img, np.float32)


def g_flat(rng, h, w, t, o):
    """Flat colour with a soft vertical gradient (sky fills)."""
    top = rgb_from(o.get('top', t[1]))
    bottom = rgb_from(o.get('bottom', t[2]))
    g = np.linspace(0, 1, h, dtype=np.float32)[:, None, None]
    return np.broadcast_to(top * (1 - g) + bottom * g, (h, w, 3)).copy()


def g_sky(rng, h, w, t, o):
    """Sky backdrop: a vertical gradient with soft drifting clouds."""
    dark, mid, light = t
    g = np.linspace(0, 1, h, dtype=np.float32)[:, None, None]
    base = np.broadcast_to(mid * (1 - g) + light * g, (h, w, 3)).copy()
    clouds = np.clip((stretched(rng, h, w, 4, 1, 2.2) - 0.5) * 2.2, 0, 1)[..., None] * o.get('clouds', 0.6)
    return base * (1 - clouds) + np.maximum(light, mid) * 1.08 * clouds


def g_glow(rng, h, w, t, o):
    """Additive effect / light glow: a soft radial blob of the brightest tone on black."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    r = np.hypot(xx / w - 0.5, yy / h - 0.5) * 2
    n = 0.75 + 0.5 * fbm(rng, h, w, 1.6)
    a = np.clip(1 - r, 0, 1) ** 1.6 * n
    return t[2][None, None, :] * a[..., None]


def g_screen(rng, h, w, t, o):
    """Console / monitor face: dark glass with lit bars and boxes."""
    dark, mid, light = t
    rgb = np.broadcast_to(dark * 0.35, (h, w, 3)).copy()
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
    glow = tuple(int(c) for c in np.clip(np.maximum(light, mid * 1.4), 0, 255))
    for _ in range(int(rng.integers(5, 11))):
        x0, y0 = rng.uniform(0.08, 0.8) * w, rng.uniform(0.08, 0.85) * h
        x1, y1 = x0 + rng.uniform(0.05, 0.35) * w, y0 + rng.uniform(0.02, 0.12) * h
        if rng.random() < 0.5:
            d.rectangle((x0, y0, min(x1, w * 0.92), min(y1, h * 0.92)), outline=glow, width=max(1, w // 128))
        else:
            d.rectangle((x0, y0, min(x1, w * 0.92), min(y1, h * 0.92)), fill=glow)
    d.rectangle((0, 0, w - 1, h - 1), outline=tuple(int(c) for c in mid), width=max(2, w // 32))
    return np.asarray(img, np.float32)


def g_window(rng, h, w, t, o):
    """Window: panes of tinted glass in a frame (grid of nx x ny)."""
    dark, mid, light = t
    nx, ny = o.get('nx', 2), o.get('ny', 2)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    fu, fv = (xx / w * nx) % 1, (yy / h * ny) % 1
    frame = (np.minimum(fu, 1 - fu) < 0.06) | (np.minimum(fv, 1 - fv) < 0.06)
    glass = ramp(0.35 + 0.5 * (1 - yy / h) * 0.6 + 0.15 * stretched(rng, h, w, 1, 6), dark, mid, light)
    sheen = np.clip(1 - np.abs(((xx + yy * 0.6) / w) % 1 - 0.3) * 6, 0, 1) * 0.25
    glass = glass * (1 + sheen[..., None])
    rgb = glass.copy()
    rgb[frame] = (dark * 0.7)[None, :]
    return rgb


GENERATORS = {
    'noise': g_noise, 'bricks': g_bricks, 'tiles': g_tiles, 'planks': g_planks, 'plywood': g_plywood,
    'corrugated': g_corrugated, 'siding': g_siding, 'treadplate': g_treadplate, 'grate': g_grate,
    'panel': g_panel, 'pipes': g_pipes, 'lamp': g_lamp, 'sign': g_sign, 'flat': g_flat,
    'sky': g_sky, 'glow': g_glow, 'screen': g_screen, 'window': g_window,
}


# --- driver -----------------------------------------------------------------------------------

def original(key: str) -> tuple[Image.Image | None, int, int]:
    """The converted original (work/out, local only) for its colours and size."""
    man = MFPORT / 'work' / 'out' / 'textures.json'
    if man.exists():
        tex = {k.lower(): v for k, v in json.loads(man.read_text()).items()}
        info = tex.get(key.lower())
        if info:
            f = MFPORT / 'work' / 'out' / info['file']
            img = Image.open(f) if f.exists() else None
            return img, info['usize'], info['vsize']
    return None, 256, 256


def make(key: str, spec: dict) -> Image.Image:
    src, uw, vh = (None, spec['size'][0], spec['size'][1]) if 'size' in spec else original(key)
    scale = spec.get('scale', 2)                      # MF 256 -> 512 (our cap), 128 -> 256
    w, h = min(512, uw * scale), min(512, vh * scale)
    rng = np.random.default_rng(zlib.crc32(key.lower().encode()))
    t = tones(src)
    for i, name in enumerate(('dark', 'mid', 'light')):
        if name in spec:
            t[i] = rgb_from(spec[name])
    out = GENERATORS[spec['gen']](rng, h, w, t, spec)
    rgb, alpha = out if isinstance(out, tuple) else (out, None)
    # surface detail on top of every material: fine grit and big soft grime patches
    plain = spec['gen'] in ('lamp', 'sign', 'flat', 'sky', 'glow', 'screen')
    grit = spec.get('grit', 0.0 if plain else 0.22)
    grime = spec.get('grime', 0.0 if plain else 0.25)
    if grit:
        rgb = shade(rgb, 1 - grit / 2 + grit * fbm(rng, h, w, 0.6))
    if grime:
        rgb = shade(rgb, 1 - grime * np.clip(fbm(rng, h, w, 2.4) - 0.45, 0, 1) * 1.8)
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), 'RGB')
    if alpha is not None:
        img.putalpha(Image.fromarray(alpha.astype(np.uint8)))
    return img


# --- automatic specs ------------------------------------------------------------------------

KEYWORDS = [   # first match wins: (words in the key, generator, options)
    (('sky', 'cloud', 'des_sun'), 'sky', {}),
    (('spark', 'smoke', 'flash', 'blood', 'flame', 'fire', 'glow', 'flare', 'explo', 'corona'), 'glow', {}),
    (('light', 'lamp', 'bulb'), 'lamp', {'kind': 'round'}),
    (('sign', 'poster', 'logo', 'advert', 'arrow', 'notice', 'label', 'flag', 'decal', 'stripe'), 'sign', {}),
    (('screen', 'monitor', 'console', 'terminal', 'radar', 'computer', 'tconsole', 'display'), 'screen', {}),
    (('window', 'glass', 'win_'), 'window', {}),
    (('grate', 'grill', 'mesh', 'fence', 'net', 'wire', 'vent'), 'grate', {'n': 12}),
    (('brick',), 'bricks', {}),
    (('tile', 'slab', 'floortile'), 'tiles', {}),
    (('plank', 'wood', 'board', 'ply', 'lumber_wood'), 'planks', {}),
    (('crate', 'box', 'cargo', 'container', 'locker'), 'panel', {'border': 0.07}),
    (('door',), 'panel', {'border': 0.06, 'bolt': True}),
    (('pipe', 'tube'), 'pipes', {}),
    (('roof', 'corrug', 'sheet', 'cladding'), 'corrugated', {}),
    (('metal', 'steel', 'iron', 'rust', 'crane', 'girder', 'beam'), 'panel', {'border': 0.0}),
    (('ground', 'dirt', 'sand', 'mud', 'grass', 'snow', 'rock', 'stone', 'gravel', 'road', 'concrete',
      'floor', 'wall', 'plaster', 'cement'), 'noise', {}),
]


def periodicity(gray: np.ndarray) -> tuple[int, float, int, float]:
    """Strongest repeat count along x and y (2..32) and how much it stands out (peak / mean)."""
    out = []
    for axis in (0, 1):
        prof = gray.mean(axis=axis)
        prof = prof - prof.mean()
        spec = np.abs(np.fft.rfft(prof)) ** 2
        band = spec[2:33]
        k = int(np.argmax(band)) + 2
        out += [k, float(band.max() / (spec[1:].mean() + 1e-6))]
    return out[0], out[1], out[2], out[3]   # columns (x): repeats along x; rows (y)


def auto_spec(key: str, img: Image.Image, modes: set[str]) -> dict | None:
    k = key.lower()
    rgb = img.convert('RGB')
    gray = np.asarray(rgb.convert('L').resize((128, 128)), np.float32)
    std = float(gray.std())
    dark, mid, light = tones(rgb)
    spec: dict = {'size': list(img.size)}
    gen, opts = None, {}
    for words, g, o in KEYWORDS:
        if any(w in k for w in words):
            gen, opts = g, dict(o)
            break
    if 'masked' in modes and gen != 'grate':
        return None                                   # see-through shapes (foliage, decals): later, by hand
    if 'additive' in modes and gen not in ('glow', 'lamp'):
        gen, opts = 'glow', {}
    nx, px, ny, py = periodicity(gray)
    ex = float(np.abs(np.diff(gray, axis=1)).mean())   # edges across x (vertical lines)
    ey = float(np.abs(np.diff(gray, axis=0)).mean())   # edges across y (horizontal lines)
    if gen in (None, 'noise', 'corrugated', 'panel') and py > 12 and ny >= 4 and 0.45 < ex / max(ey, 1e-3) < 1.1 and 'roof' not in k:
        gen = 'bricks'   # rows of courses with short joints between: a brick / block wall
    if gen in (None, 'noise', 'panel', 'corrugated', 'planks', 'tiles', 'bricks'):
        strong_x, strong_y = px > 12, py > 12
        if gen == 'bricks':
            opts.update(rows=int(min(16, max(4, ny))), cols=int(min(8, max(2, nx // 2 or 2))))
        elif gen == 'tiles' or (gen in (None, 'noise') and strong_x and strong_y):
            gen = 'tiles'
            opts.update(n=int(min(8, max(2, round((nx + ny) / 2)))), grout=0.04)
        elif gen == 'planks' or (gen in (None, 'noise') and strong_y and not strong_x and 'wood' in k):
            gen = 'planks'
            opts.update(n=int(min(10, max(1, ny if strong_y else 4))), vertical=bool(strong_x and not strong_y))
        elif strong_x != strong_y and gen in (None, 'noise', 'panel', 'corrugated'):
            n = nx if strong_x else ny
            gen = 'corrugated' if n >= 6 else 'siding'
            opts = {'ridges': int(min(24, n)), 'vertical': bool(strong_x)} if gen == 'corrugated' else {'n': int(n), 'vertical': bool(strong_x)}
        elif gen is None:
            gen = 'noise'
    if gen == 'noise':
        opts.setdefault('contrast', round(float(np.clip(std / 32.0, 0.4, 1.6)), 2))
        opts.setdefault('beta', 1.8)
        dark_frac = float((gray < gray.mean() - 2.2 * std).mean()) if std > 1 else 0.0
        if dark_frac > 0.01:
            opts['spots'] = round(min(0.2, dark_frac * 4), 3)
    if gen == 'grate' and 'masked' in modes:
        opts['masked'] = True
    if gen == 'sign':
        hsv = np.asarray(rgb.convert('HSV').resize((32, 32)), np.float32).reshape(-1, 3)
        vivid = hsv[hsv[:, 1] > 80]
        base = np.asarray(rgb.resize((32, 32)), np.float32).reshape(-1, 3)
        col = base[hsv[:, 1] > 80].mean(axis=0) if len(vivid) > 20 else mid
        opts.update(color=[int(c) for c in col], mark=[255, 255, 255] if col.mean() < 150 else [30, 30, 30])
    if gen == 'window':
        opts.update(nx=int(min(6, max(1, nx if px > 8 else 2))), ny=int(min(6, max(1, ny if py > 8 else 2))))
    spec.update(gen=gen, dark=[int(c) for c in dark], mid=[int(c) for c in mid], light=[int(c) for c in light])
    spec.update(opts)
    return spec


def needed_textures() -> dict[str, set[str]]:
    """Textures core maps use from classic packages, with the modes they're drawn in."""
    sys.path.insert(0, str(MFPORT))
    from mflib import config, packs
    cfg = config.load()
    p = packs.load()
    need: dict[str, set[str]] = {}
    for lj in (cfg.out_dir / 'Maps').glob('*/level.json'):
        if packs.map_pack(p, lj.parent.name) != 'community':
            continue
        for key, modes in json.loads(lj.read_text())['texture_usage'].items():
            pkg = key.split('.')[0]
            if set(modes) <= {'sky'} or pkg.lower() == 'engine' or packs.package_pack(p, pkg) != 'classic':
                continue
            need.setdefault(key.lower(), set()).update(modes)
    return need


def write_auto_table() -> None:
    hand = {k.lower() for k in tomllib.loads(TABLE.read_text())}
    man = {k.lower(): (k, v) for k, v in json.loads((MFPORT / 'work' / 'out' / 'textures.json').read_text()).items()}
    lines = ['# Written by `lookalike.py --auto` (re-run after converting more core maps); edit an entry or',
             '# move it to lookalikes.toml to tune it. Colours were sampled from the originals.', '']
    skipped, n = [], 0
    for lk, modes in sorted(needed_textures().items()):
        if lk in hand or lk not in man:
            continue
        key, info = man[lk]
        f = MFPORT / 'work' / 'out' / info['file']
        if not f.exists():
            continue
        spec = auto_spec(key, Image.open(f), modes)
        if spec is None:
            skipped.append(key)
            continue
        lines.append(f'["{key}"]')
        for name, val in spec.items():
            lines.append(f'{name} = {json.dumps(val)}')
        lines.append('')
        n += 1
    if skipped:
        lines.append('# not generated (see-through shapes; replace by hand): ' + ', '.join(skipped))
    AUTO_TABLE.write_text('\n'.join(lines) + '\n')
    print(f'[lookalike] {n} automatic entries -> {AUTO_TABLE.name} ({len(skipped)} left for hand-made art)')


def main(argv: list[str]) -> None:
    if '--auto' in argv:
        write_auto_table()
        if len([a for a in argv if not a.startswith('--')]) == 0 and '--sheet' not in argv:
            return
    table = tomllib.loads(TABLE.read_text())
    if AUTO_TABLE.exists():
        for key, spec in tomllib.loads(AUTO_TABLE.read_text()).items():
            table.setdefault(key, spec)
    want = [a for a in argv if not a.startswith('--')]
    sheet = []
    n = 0
    for key, spec in table.items():
        if want and not any(w.lower() in key.lower() for w in want):
            continue
        img = make(key, spec)
        parts = key.split('.')
        dst = OUT.joinpath(*parts[:-1], parts[-1] + '.png')
        dst.parent.mkdir(parents=True, exist_ok=True)
        img.save(dst, optimize=True)
        n += 1
        if len(sheet) < 120:
            sheet.append((key, img))
    print(f'[lookalike] {n} textures -> {OUT}')
    if '--sheet' in argv and sheet:
        cell = 160
        cols = 6
        out = Image.new('RGB', (cols * cell * 2, ((len(sheet) + cols - 1) // cols) * cell), (0, 0, 0))
        for i, (key, img) in enumerate(sheet):
            src, _, _ = original(key)
            x, y = (i % cols) * cell * 2, (i // cols) * cell
            if src:
                out.paste(src.convert('RGB').resize((cell, cell)), (x, y))
            out.paste(img.resize((cell, cell)), (x + cell, y))
            ImageDraw.Draw(out).text((x + 2, y + 2), key.split('.')[-1], fill=(255, 255, 0))
        path = HERE / 'preview' / 'lookalikes_sheet.png'   # not out/: that is imported as game art
        path.parent.mkdir(parents=True, exist_ok=True)
        out.save(path)
        print(f'[lookalike] comparison sheet -> {path}')


if __name__ == '__main__':
    main(sys.argv[1:])
