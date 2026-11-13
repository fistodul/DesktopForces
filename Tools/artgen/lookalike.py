#!/usr/bin/env python3
"""Replacement art: new procedural textures that stand in for original Mobile Forces ones.

Community and unverified maps (Tools/mfport/packs.toml) borrow textures from packs they can't
count on. This makes our own versions: each entry names a procedural
generator (bricks, planks, corrugated metal, plaster ...) and its layout; only the original's
*colours* are sampled (dark / mid / light tones), never its pixels.

  lookalikes.toml       hand-tuned entries
  lookalikes_auto.toml  written by --auto for every other texture those maps still borrow: the
                        generator picked from the name and the stripes / grid found in the
                        original, colours and size stored, so it regenerates without the game

Output: Tools/mfport/work/generated/textures/<package>/<group>/<name>.png (git-ignored,
re-created by this script), which the pipeline takes as replacement art after any hand-made
file in Tools/mfport/overrides/textures (committed, wins).

    python3 Tools/artgen/lookalike.py --auto       # classify what non-classic maps newly need
    python3 Tools/artgen/lookalike.py --auto --reclassify   # ask again for every automatic entry
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
    return tones_of(np.asarray(src.convert('RGB').resize((64, 64)), np.float32).reshape(-1, 3))


def tones_of(a: np.ndarray) -> list[np.ndarray]:
    """tones() of a list of pixels (n, 3): part of a picture, say a wall without its window."""
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
    # in standard deviations: fbm's range depends on its one brightest peak, and dark / light are
    # the original's 10th and 90th percentile, which +-1.28 of them reach at contrast 1
    v = 0.5 + (n - n.mean()) / (n.std() + 1e-6) * 0.39 * o.get('contrast', 1.0)
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
    if o.get('spots', 0):
        base = shade(base, 1 - 0.6 * speckle(rng, h, w, o['spots'], o.get('spot_size', 2)))
    if o.get('flecks', 0):
        base = shade(base, 1 + 0.35 * speckle(rng, h, w, o['flecks'], o.get('spot_size', 2)))
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


def drawn_mask(h, w, draw) -> np.ndarray:
    """Coverage 0..255 of shapes drawn at twice the size (draw(d, W, H)), for smooth edges."""
    m = Image.new('L', (w * 2, h * 2), 0)
    draw(ImageDraw.Draw(m), w * 2, h * 2)
    return np.asarray(m.resize((w, h), Image.LANCZOS), np.float32)


def g_lattice(rng, h, w, t, o):
    """Steel frames (rails, posts, cross braces, walkway grids): see-through between the bars."""
    dark, mid, light = t
    rows, cols = o.get('rows', 1), o.get('cols', 1)
    thick = o.get('thick', 0.03)

    def draw(d, W, H):
        T = max(2, int(thick * W))
        for i in range(rows + 1 if rows else 0):
            y = i * (H - T) / rows
            d.rectangle((0, y, W, y + T), fill=255)
        for i in range(cols + 1 if cols else 0):
            x = i * (W - T) / cols
            d.rectangle((x, 0, x + T, H), fill=255)
        if o.get('cross'):
            cw, ch = W / max(cols, 1), H / max(rows, 1)
            for i in range(max(cols, 1)):
                for j in range(max(rows, 1)):
                    d.line((i * cw, j * ch, (i + 1) * cw, (j + 1) * ch), fill=255, width=T)
                    d.line(((i + 1) * cw, j * ch, i * cw, (j + 1) * ch), fill=255, width=T)
    rgb = ramp(0.3 + 0.5 * fbm(rng, h, w, 1.7), dark, mid, light)
    return rgb, drawn_mask(h, w, draw)


def g_tree(rng, h, w, t, o):
    """A tree on a card: forking branches, leaves on the twigs (leaves = 0: bare)."""
    dark, mid, light = t
    bark = rgb_from(o.get('bark', dark * 0.7))
    img = Image.new('RGB', (w * 2, h * 2), tuple(int(c) for c in bark))
    mask = Image.new('L', (w * 2, h * 2), 0)
    di, dm = ImageDraw.Draw(img), ImageDraw.Draw(mask)
    leaves = o.get('leaves', 0.5)
    size = o.get('leaf', 0.025) * w * 2

    def branch(x, y, angle, length, width, depth):
        x2, y2 = x + np.sin(angle) * length, y - np.cos(angle) * length
        for d in (di, dm):
            d.line((x, y, x2, y2), fill=(tuple(int(c) for c in bark) if d is di else 255), width=max(2, int(width)))
        if depth == 0:
            for _ in range(int(14 * leaves)):
                lx, ly = x2 + rng.normal(0, size * 2.2), y2 + rng.normal(0, size * 2.2)
                r = size * rng.uniform(0.6, 1.2)
                col = tuple(int(c) for c in ramp(np.array([rng.random()], np.float32), dark, mid, light)[0])
                di.ellipse((lx - r, ly - r * 0.6, lx + r, ly + r * 0.6), fill=col)
                dm.ellipse((lx - r, ly - r * 0.6, lx + r, ly + r * 0.6), fill=255)
            return
        for _ in range(o.get('forks', 3)):
            branch(x2, y2, angle + rng.uniform(-0.75, 0.75), length * rng.uniform(0.6, 0.82), width * 0.68, depth - 1)
    branch(w, h * 2, 0.0, h * 0.62, w * o.get('trunk', 0.035) * 2, o.get('depth', 5))
    rgb = np.asarray(img.resize((w, h), Image.LANCZOS), np.float32)
    return rgb, np.asarray(mask.resize((w, h), Image.LANCZOS), np.float32)


def g_palm(rng, h, w, t, o):
    """Palm fronds side by side: blades fanning off a pale rib along the middle."""
    dark, mid, light = t
    img = Image.new('RGB', (w * 2, h * 2), (0, 0, 0))
    mask = Image.new('L', (w * 2, h * 2), 0)
    di, dm = ImageDraw.Draw(img), ImageDraw.Draw(mask)
    H, W = h * 2, w * 2
    for x in np.linspace(0, W, o.get('blades', 64)):
        for side in (-1, 1):
            reach = H * rng.uniform(0.3, 0.48) * (0.5 + 0.5 * np.sin(np.pi * x / W))
            tip = (x + reach * rng.uniform(0.5, 1.0), H / 2 + side * reach)
            blade = [(x - W * 0.03, H / 2), tip, (x + W * 0.03, H / 2)]
            col = tuple(int(c) for c in ramp(np.array([rng.random()], np.float32), dark, mid, light)[0])
            di.polygon(blade, fill=col)
            dm.polygon(blade, fill=255)
    di.rectangle((0, H / 2 - H * 0.012, W, H / 2 + H * 0.012), fill=tuple(int(c) for c in light))
    dm.rectangle((0, H / 2 - H * 0.012, W, H / 2 + H * 0.012), fill=255)
    return np.asarray(img.resize((w, h), Image.LANCZOS), np.float32), np.asarray(mask.resize((w, h), Image.LANCZOS), np.float32)


def g_splat(rng, h, w, t, o):
    """A splash: one blot and a few drops around it, soft edged."""
    dark, mid, _ = t

    def draw(d, W, H):
        for i in range(o.get('drops', 7)):
            a, r = rng.uniform(0, 6.28), 0 if i == 0 else rng.uniform(0.2, 0.42) * W
            s = (0.2 if i == 0 else rng.uniform(0.03, 0.09)) * W
            x, y = W / 2 + np.cos(a) * r, H / 2 + np.sin(a) * r
            d.ellipse((x - s, y - s, x + s, y + s), fill=255)
    alpha = blur(drawn_mask(h, w, draw) / 255, max(w / 32, 1)) * 255
    return np.broadcast_to(mid, (h, w, 3)).copy(), np.clip((alpha - 60) * 1.6, 0, 255)


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


# Words of our own, never the originals': a shop front's names, a work site's notices
SHOP_WORDS = ['BAKERY', 'CAFE', 'BOOKS', 'PHARMACY', 'MARKET', 'HOTEL', 'FLOWERS', 'TOOLS', 'RADIO', 'CLINIC',
              'LAUNDRY', 'BARBER', 'KIOSK', 'GARAGE', 'BANK', 'POST', 'TAILOR', 'GROCER', 'FISH', 'FRUIT',
              'TOYS', 'MUSIC', 'SHOES', 'TO LET', 'OPEN', 'SALE', 'CINEMA', 'TAXI', 'PIZZA', 'OFFICE']
SITE_WORDS = ['DEPOT', 'CARGO', 'ZONE 3', 'NO ENTRY', 'EXIT', 'STORE', 'OFFICE', 'DOCK 2', 'SECTOR 7', 'DANGER',
              'KEEP OUT', 'LOADING', 'PARTS', 'SERVICE', 'POWER', 'FUEL', 'STAFF ONLY', 'BAY 4', 'CAUTION']


def g_sign(rng, h, w, t, o):
    """Our own sign. `panels` ([[x0, y0, x1, y1, [r, g, b], [r, g, b]], ...], 0..1, each with its
    colour and its lettering's) is the original's layout: a sheet of four shop signs gets four
    signs, each with a whole word, because the map shows them one at a time. Without panels the
    texture is one sign. style = 'emblem' / 'stripes' for a mark or hazard stripes instead of words."""
    from PIL import ImageFont
    col = rgb_from(o.get('color', t[2]))
    mark = [int(c) for c in o.get('mark', (255, 255, 255))]
    rgb = shade(np.broadcast_to(col, (h, w, 3)).copy(), 0.95 + 0.08 * fbm(rng, h, w, 1.2))
    style = o.get('style', 'words')
    if style == 'emblem':
        return emblem(rgb, {'size': 0.32, 'color': mark, 'opacity': 1.0})
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
    if style == 'stripes':
        step = max(8, min(w, h) // 5)
        for x in range(-h, w + h, step * 2):
            d.polygon([(x, h), (x + step, h), (x + step + h, 0), (x + h, 0)], fill=tuple(mark))
        return np.asarray(img, np.float32)
    panels = o.get('panels') or [[0, 0, 1, 1, [int(c) for c in col], mark]]
    pool = list(SHOP_WORDS if o.get('shop', len(panels) > 1) else SITE_WORDS)
    for x0, y0, x1, y1, back, ink in panels:
        box = (round(x0 * w), round(y0 * h), round(x1 * w) - 1, round(y1 * h) - 1)
        pw, ph = box[2] - box[0] + 1, box[3] - box[1] + 1
        d.rectangle(box, fill=tuple(back))
        edge = max(2, min(pw, ph) // 16)
        if min(pw, ph) > 4 * edge:
            d.rectangle((box[0] + edge, box[1] + edge, box[2] - edge, box[3] - edge), outline=tuple(ink), width=max(1, edge // 2))
        words = o['words'] if 'words' in o and len(panels) == 1 else pool.pop(int(rng.integers(0, len(pool))))
        if not pool:
            pool = list(SHOP_WORDS)
        size = max(8, int(ph * 0.5))
        font = ImageFont.load_default(size=size)
        tb = d.textbbox((0, 0), words, font=font)
        while size > 8 and tb[2] - tb[0] > (pw - 6 * edge) * 0.92:   # shrink to fit inside the border
            size = int(size * 0.88)
            font = ImageFont.load_default(size=size)
            tb = d.textbbox((0, 0), words, font=font)
        if size >= max(10, h * 0.06):                 # a sliver stays a plain plate: specks of lettering read as dirt
            d.text((box[0] + (pw - (tb[2] - tb[0])) / 2 - tb[0], box[1] + (ph - (tb[3] - tb[1])) / 2 - tb[1]), words, font=font, fill=tuple(ink))
    return np.asarray(img, np.float32)


def g_checker(rng, h, w, t, o):
    """Flat squares of two colours, n x n (the ball skin: traffic cones wear its red and white)."""
    n = o.get('n', 2)
    a, b = (rgb_from(c) for c in o.get('colors', (t[0], t[2])))
    yy, xx = np.mgrid[0:h, 0:w]
    odd = ((yy * n // h) + (xx * n // w)) % 2 == 1
    return np.where(odd[..., None], b, a).astype(np.float32)


def bevel_box(d, box, face, light, dark, width):
    """A raised (or, colours swapped, sunk) panel: lit from the top left."""
    x0, y0, x1, y1 = box
    d.rectangle(box, fill=face)
    for i in range(width):
        d.line([(x0 + i, y1 - i), (x0 + i, y0 + i), (x1 - i, y0 + i)], fill=light)
        d.line([(x1 - i, y0 + i), (x1 - i, y1 - i), (x0 + i, y1 - i)], fill=dark)


def g_door(rng, h, w, t, o):
    """A door, so that a doorway reads as one: frame, one or two leaves of raised panels (or
    glazed), a handle by the meeting edge. leaves 1 | 2, rows of panels, glass."""
    dark, mid, light = t
    leaves, rows, glass = o.get('leaves', 1), o.get('rows', 3), o.get('glass', False)
    grain = ramp(0.38 + 0.3 * stretched(rng, h, w, 1, 9) + 0.12 * fbm(rng, h, w, 1.8), dark, mid, light)
    img = Image.fromarray(np.clip(grain, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
    c = lambda v: tuple(int(x) for x in np.clip(v, 0, 255))
    fw = max(2, w // 28)
    d.rectangle((0, 0, w - 1, h - 1), outline=c(dark * 0.55), width=fw)                   # frame
    lw = (w - 2 * fw) / leaves
    for leaf in range(leaves):
        x0, x1 = fw + leaf * lw, fw + (leaf + 1) * lw
        d.line([(x1, fw), (x1, h - fw)], fill=c(dark * 0.4), width=max(1, fw // 2))      # meeting edge
        stile = lw * 0.13
        for r in range(rows):
            y0 = fw + stile + r * (h - 2 * fw - stile) / rows
            y1 = fw + (r + 1) * (h - 2 * fw - stile) / rows
            box = (int(x0 + stile), int(y0), int(x1 - stile), int(y1))
            if box[2] - box[0] < 4 or box[3] - box[1] < 4:
                continue
            if glass:
                if r == 0:   # one pane the leaf's height, a push bar across it
                    pane = (box[0], box[1], box[2], int(h - fw - stile))
                    d.rectangle((pane[0] - fw, pane[1] - fw, pane[2] + fw, pane[3] + fw), fill=c(light))
                    d.rectangle(pane, fill=c(dark * 0.35 + np.array([4, 8, 14])))
                    d.polygon([(pane[0], pane[3]), (pane[0], pane[1] + (pane[3] - pane[1]) * 0.6), (pane[0] + (pane[2] - pane[0]) * 0.6, pane[1]),
                               (pane[2], pane[1]), (pane[2], pane[1] + (pane[3] - pane[1]) * 0.1)], fill=c(dark * 0.5 + np.array([8, 14, 22])))
                    d.rectangle((pane[0], int(h * 0.5), pane[2], int(h * 0.5) + fw), fill=c(light * 0.9))
            else:
                bevel_box(d, box, c(mid * 0.92), c(dark * 0.6), c(light), max(1, fw // 2))   # sunk field
                inner = (box[0] + fw, box[1] + fw, box[2] - fw, box[3] - fw)
                if inner[2] > inner[0] + 4 and inner[3] > inner[1] + 4:
                    bevel_box(d, inner, c(mid), c(light), c(dark * 0.6), max(1, fw // 2))   # raised panel
        hx = x1 - stile * 0.5 if leaf == 0 else x0 + stile * 0.5                          # handles meet
        hr = max(2, w // 64)
        if glass:
            continue
        d.ellipse((hx - hr, h * 0.52 - hr * 2.2, hx + hr, h * 0.52 + hr * 2.2), fill=c(dark * 0.35))
        d.ellipse((hx - hr * 0.5, h * 0.52 - hr * 1.6, hx + hr * 0.3, h * 0.52 - hr * 0.4), fill=c(light * 1.1))
    return np.asarray(img, np.float32) * (0.9 + 0.2 * fbm(rng, h, w, 1.2))[..., None]


def g_disc(rng, h, w, t, o):
    """A round face on its backdrop (barrel ends, drain covers, pipe mouths): the map shows the
    circle and hides the corners. back = corner colour, grid = a cover's tread, hole = a dark middle."""
    dark, mid, light = t
    back = rgb_from(o.get('back', dark * 0.3))
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    r = np.hypot(xx / w - 0.5, yy / h - 0.5) * 2 / o.get('fill', 0.96)
    face = ramp(0.45 + 0.3 * fbm(rng, h, w, 1.8) + 0.12 * np.cos(r * 9), dark, mid, light)
    if o.get('grid'):
        n = o['grid']
        bars = (((xx / w * n) % 1) < 0.18) | (((yy / h * n) % 1) < 0.18)
        face = np.where(bars[..., None], face * 0.62, face * 1.08)
    rim = (r > 0.86) & (r <= 1)
    face = np.where(rim[..., None], shade(face, 1.25 - (r[..., None] - 0.86) * 3), face)
    if o.get('hole'):
        face = np.where((r < o['hole'])[..., None], dark * 0.15, face)
    else:
        cap = np.hypot(xx / w - 0.3, yy / h - 0.3) * 2 < 0.1
        face = np.where(cap[..., None], light * 0.9, face)
    edge = np.clip((1 - r) * w / 4, 0, 1)[..., None]
    return face * edge + back * (1 - edge)


def g_machine(rng, h, w, t, o):
    """A vending machine's front: a lit window with rows of goods (colours given), a panel with
    buttons and a slot beside it, the tray below."""
    dark, mid, light = t
    body = ramp(0.45 + 0.2 * fbm(rng, h, w, 1.8), dark, mid, light)
    img = Image.fromarray(np.clip(body, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
    c = lambda v: tuple(int(x) for x in np.clip(v, 0, 255))
    goods = [rgb_from(g) for g in o.get('goods', ((200, 60, 50), (240, 190, 40), (70, 150, 80), (60, 110, 190)))]
    d.rectangle((0, 0, w - 1, h - 1), outline=c(dark * 0.5), width=max(2, w // 40))
    wx0, wy0, wx1, wy1 = int(w * 0.07), int(h * 0.07), int(w * 0.68), int(h * 0.78)
    d.rectangle((wx0, wy0, wx1, wy1), fill=c(dark * 0.3), outline=c(light), width=max(1, w // 90))
    rows, cols = 5, 5
    for r in range(rows):
        y0 = wy0 + (r + 0.12) * (wy1 - wy0) / rows
        y1 = wy0 + (r + 0.86) * (wy1 - wy0) / rows
        d.line([(wx0 + 2, y1 + 2), (wx1 - 2, y1 + 2)], fill=c(mid * 0.8), width=max(1, h // 128))   # shelf
        for k in range(cols):
            x0 = wx0 + (k + 0.16) * (wx1 - wx0) / cols
            x1 = wx0 + (k + 0.84) * (wx1 - wx0) / cols
            g = goods[int(rng.integers(0, len(goods)))]
            d.rounded_rectangle((x0, y0, x1, y1), radius=max(1, w // 100), fill=c(g))
            d.rectangle((x0 + (x1 - x0) * 0.2, y0 + (y1 - y0) * 0.3, x1 - (x1 - x0) * 0.2, y0 + (y1 - y0) * 0.6), fill=c(g * 0.5 + 120))
    px0, px1 = int(w * 0.74), int(w * 0.93)
    d.rectangle((px0, wy0, px1, int(h * 0.5)), fill=c(dark * 0.6), outline=c(light * 0.9))
    for i in range(6):
        bx, by = px0 + (0.2 + 0.4 * (i % 2)) * (px1 - px0), wy0 + (0.12 + 0.13 * (i // 2)) * h
        d.rectangle((bx, by, bx + (px1 - px0) * 0.25, by + h * 0.05), fill=c(light))
    d.rectangle((px0 + (px1 - px0) * 0.3, int(h * 0.58), px1 - (px1 - px0) * 0.3, int(h * 0.6)), fill=c(dark * 0.2))   # coin slot
    d.rectangle((int(w * 0.14), int(h * 0.84), int(w * 0.6), int(h * 0.93)), fill=c(dark * 0.35), outline=c(light * 0.8))   # tray
    return np.asarray(img, np.float32)


def window_inset(rgb, spec, t):
    """A window let into any wall (`window = [x0, y0, x1, y1]`, `glass`, `sill` colours): frame,
    glazing bars, dark glass with a sheen, a sill casting a line of shadow."""
    h, w, _ = rgb.shape
    x0, y0, x1, y1 = (int(round(v * s)) for v, s in zip(spec['window'], (w, h, w, h)))
    glass = rgb_from(spec.get('glass', t[0] * 0.5))
    trim = rgb_from(spec.get('sill', t[2]))
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
    c = lambda v: tuple(int(x) for x in np.clip(v, 0, 255))
    fw = max(2, (x1 - x0) // 14)
    d.rectangle((x0 - fw, y0 - fw, x1 + fw, y1 + fw), fill=c(trim * 0.8))
    d.rectangle((x0, y0, x1, y1), fill=c(glass))
    d.polygon([(x0, y1), (x0, y0 + (y1 - y0) * 0.55), (x0 + (x1 - x0) * 0.5, y0), (x0 + (x1 - x0) * 0.8, y0), (x0, y1 - (y1 - y0) * 0.1)], fill=c(glass * 1.25 + 10))
    nx, ny = (2, 3) if (y1 - y0) > (x1 - x0) * 1.2 else (2, 2)
    for i in range(1, nx):
        d.line([(x0 + (x1 - x0) * i / nx, y0), (x0 + (x1 - x0) * i / nx, y1)], fill=c(trim * 0.8), width=max(1, fw // 2))
    for i in range(1, ny):
        d.line([(x0, y0 + (y1 - y0) * i / ny), (x1, y0 + (y1 - y0) * i / ny)], fill=c(trim * 0.8), width=max(1, fw // 2))
    d.rectangle((x0 - 2 * fw, y1 + fw, x1 + 2 * fw, y1 + 2 * fw), fill=c(trim))
    d.line([(x0 - 2 * fw, y1 + 2 * fw + 1), (x1 + 2 * fw, y1 + 2 * fw + 1)], fill=c(t[0] * 0.5), width=max(1, fw // 2))
    return np.asarray(img, np.float32)


def g_bands(rng, h, w, t, o):
    """Soft colour bands across the texture, top to bottom: stops [[y 0..1, [r, g, b]], ...]
    (the spawn room force fields)."""
    stops = o['stops']
    ys = np.array([s[0] for s in stops], np.float32) * (h - 1)
    y = np.arange(h, dtype=np.float32)
    rgb = np.stack([np.interp(y, ys, [s[1][i] for s in stops]) for i in range(3)], axis=1)
    return np.broadcast_to(rgb[:, None, :], (h, w, 3)).copy()


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
    'bands': g_bands, 'lattice': g_lattice, 'tree': g_tree, 'palm': g_palm, 'splat': g_splat,
    'checker': g_checker, 'door': g_door, 'disc': g_disc, 'machine': g_machine,
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


def relief(rng, h, w, strength: float) -> np.ndarray:
    """Pits and bumps lit from the top left, what a surface shows only when you stand next to it
    (a brightness factor around 1, tileable)."""
    bump = fbm(rng, h, w, 2.0)
    light = (bump - np.roll(bump, 1, axis=0)) + (bump - np.roll(bump, 1, axis=1))
    return 1 + np.clip(light / (light.std() + 1e-6) * strength, -0.45, 0.45)


def detail_textures() -> set[str]:
    data = tomllib.loads((MFPORT / 'detail.toml').read_text())
    return {k.lower() for k in data['textures']}


def make(key: str, spec: dict) -> Image.Image:
    src, uw, vh = (None, spec['size'][0], spec['size'][1]) if 'size' in spec else original(key)
    scale = spec.get('scale', 2)                      # MF 256 -> 512 (our cap), 128 -> 256
    cap = 512
    if key.lower() in detail_textures():              # walked up to: Tools/mfport/detail.toml
        scale, cap = scale * 2, 1024
    w, h = min(cap, uw * scale), min(cap, vh * scale)
    rng = np.random.default_rng(zlib.crc32(key.lower().encode()))
    t = tones(src)
    for i, name in enumerate(('dark', 'mid', 'light')):
        if name in spec:
            t[i] = rgb_from(spec[name])
    out = GENERATORS[spec['gen']](rng, h, w, t, spec)
    rgb, alpha = out if isinstance(out, tuple) else (out, None)
    if 'window' in spec:
        rgb = window_inset(rgb, spec, t)
    # surface detail on top of every material: fine grit and big soft grime patches
    plain = spec['gen'] in ('lamp', 'sign', 'flat', 'sky', 'glow', 'screen', 'splat', 'bands', 'checker')
    grit = spec.get('grit', 0.0 if plain else 0.22)
    grime = spec.get('grime', 0.0 if plain else 0.25)
    if grit:
        rgb = shade(rgb, 1 - grit / 2 + grit * fbm(rng, h, w, 0.6))
    if grime:
        rgb = shade(rgb, 1 - grime * np.clip(fbm(rng, h, w, 2.4) - 0.45, 0, 1) * 1.8)
    rgb = rgb * spec.get('gain', 1.0)                 # as bright as the original, whatever the recipe did
    if key.lower() in detail_textures() and not plain:
        rgb = shade(rgb, relief(rng, h, w, spec.get('relief', 0.09)))
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), 'RGB')
    if alpha is not None:
        img.putalpha(Image.fromarray(alpha.astype(np.uint8)))
    return img


# --- automatic specs ------------------------------------------------------------------------

# Matched against the texture's group and name, not its package: rage_snow isn't snow all
# through, an airport's Terminal group holds walls, Rage.Skins.JBallMesh1 isn't wire mesh.
KEYWORDS = [   # first match wins: (words in the name, generator, options)
    (('sky', 'cloud', 'des_sun'), 'sky', {}),
    (('spark', 'smoke', 'flash', 'blood', 'flame', 'fire', 'glow', 'flare', 'explo', 'corona'), 'glow', {}),
    (('light', 'lamp', 'bulb'), 'lamp', {'kind': 'round'}),
    (('sign', 'poster', 'logo', 'advert', 'arrow', 'notice', 'label', 'flag', 'decal', 'stripe'), 'sign', {}),
    (('screen', 'monitor', 'console', 'radar', 'computer', 'display'), 'screen', {}),
    (('drink', 'snack', 'vend', 'soda', 'cola'), 'machine', {}),
    (('window', 'glass', 'win_'), 'window', {}),
    (('grate', 'grill', 'fence', 'wire', 'vent', 'chainlink', 'netting'), 'grate', {'n': 12}),
    (('brick', 'brck'), 'bricks', {}),
    (('tile', 'slab', 'floortile'), 'tiles', {}),
    (('plank', 'wood', 'board', 'ply'), 'planks', {}),
    (('crate', 'box', 'cargo', 'container', 'locker'), 'panel', {'border': 0.07}),
    (('roller', 'shutter'), 'corrugated', {'ridges': 20, 'vertical': False}),
    (('door',), 'door', {}),
    (('pipe', 'tube'), 'pipes', {}),
    (('roof', 'corrug', 'sheet', 'cladding'), 'corrugated', {}),
    (('metal', 'steel', 'iron', 'rust', 'crane', 'girder', 'beam'), 'panel', {'border': 0.0}),
    # nature has no courses or seams: never bricks, tiles or boards, whatever repeats in it
    (('snow', 'grass', 'sand', 'dirt', 'mud', 'rock', 'gravel', 'water', 'moss', 'soil'), 'noise', {'natural': True}),
    (('ground', 'stone', 'road', 'concrete', 'floor', 'wall', 'plaster', 'cement'), 'noise', {}),
]


def subject(key: str) -> str:
    """What a texture is called: its group and name, lower case, without the package."""
    return '.'.join(key.lower().split('.')[1:]) or key.lower()


def cuts(block: np.ndarray, least: int) -> list[int]:
    """Rows at which a picture (rows, columns, 3) is ruled right across: where one sign on a sheet
    ends and the next begins. Lettering, logos and photographs change colour too, but never along
    two thirds of a straight line. Stretches come out at least `least` rows tall."""
    n = len(block)
    edge = np.zeros(n)
    edge[1:-1] = (np.abs(block[2:] - block[:-2]).sum(axis=2) > 60).mean(axis=1)
    out: list[int] = []
    for i in np.argsort(-edge):
        if edge[i] < 0.66:
            break
        if least <= i <= n - least and all(abs(i - j) >= least for j in out):
            out.append(int(i))
    return sorted(out)


def sign_panels(rgb: Image.Image) -> list:
    """The signs on a sheet: [x0, y0, x1, y1, back, ink] in 0..1 with each one's colour and its
    lettering's (colours and rectangles are all that is taken). One entry: the sheet is one sign."""
    n = 96
    a = np.asarray(rgb.resize((n, n), Image.BILINEAR), np.float32)
    out = []
    ys = [0] + cuts(a, n // 8) + [n]
    for y0, y1 in zip(ys, ys[1:]):
        xs = [0] + cuts(a[y0:y1].transpose(1, 0, 2), n // 4) + [n]
        for x0, x1 in zip(xs, xs[1:]):
            px = a[y0:y1, x0:x1].reshape(-1, 3)
            back = np.median(px, axis=0)
            far = px[np.abs(px - back).sum(axis=1) > 120]
            ink = np.median(far, axis=0) if len(far) > 0.04 * len(px) else None
            lum = lambda c: float(c @ np.array([0.299, 0.587, 0.114]))
            if ink is None or abs(lum(ink) - lum(back)) < 70:
                ink = np.array([245.0, 245.0, 240.0] if lum(back) < 140 else [30.0, 30.0, 30.0])
            out.append([round(x0 / n, 3), round(y0 / n, 3), round(x1 / n, 3), round(y1 / n, 3),
                        [int(c) for c in back], [int(c) for c in ink]])
    return out if len(out) <= 8 else out[:0]


def window_rect(gray: np.ndarray) -> list[float] | None:
    """A window in a wall texture: one dark rectangle well inside it (x0, y0, x1, y1 in 0..1)."""
    n = gray.shape[0]
    g = np.asarray(Image.fromarray(np.clip(gray, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2)), np.float32)
    dark = g < np.median(g) - max(28.0, 1.1 * g.std())
    if not 0.03 < dark.mean() < 0.45:
        return None

    def run(share: np.ndarray) -> tuple[int, int]:
        best, start = (0, 0), None
        for i, on in enumerate(list(share > 0.12) + [False]):
            if on and start is None:
                start = i
            elif not on and start is not None:
                best, start = max(best, (start, i), key=lambda r: r[1] - r[0]), None
        return best
    y0, y1 = run(dark.mean(axis=1))
    x0, x1 = run(dark.mean(axis=0))
    edge, least = n // 10, n // 10
    if min(x1 - x0, y1 - y0) < least or min(x0, y0, n - x1, n - y1) < edge:
        return None
    inside = dark[y0:y1, x0:x1]
    if inside.mean() < 0.7 or inside.sum() < 0.8 * dark.sum() or not 0.4 < (x1 - x0) / (y1 - y0) < 2.5:
        return None                                   # patchy, or dark elsewhere too: stains, not a window
    return [round(v / n, 3) for v in (x0, y0, x1, y1)]


def disc_shape(gray: np.ndarray) -> dict | None:
    """A round face on a plain backdrop (a barrel end, a drain cover): corners alike and unlike
    the middle. Says whether its centre is a dark hole."""
    n = gray.shape[0]
    c = n // 8
    corners = [gray[:c, :c], gray[:c, -c:], gray[-c:, :c], gray[-c:, -c:]]
    means = np.array([x.mean() for x in corners])
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    r = np.hypot(xx - (n - 1) / 2, yy - (n - 1) / 2) / (n / 2)
    inner, ring = gray[r < 0.8], gray[(r > 0.8) & (r < 0.93)]
    if np.ptp(means) > 14 or max(x.std() for x in corners) > 12:
        return None
    if abs(inner.mean() - means.mean()) < 45 or abs(ring.mean() - means.mean()) < 30:
        return None
    m = n // 2
    sides = np.array([gray[c // 2:c + c // 2, m - c:m + c].mean(), gray[-c - c // 2:-c // 2, m - c:m + c].mean(),
                      gray[m - c:m + c, c // 2:c + c // 2].mean(), gray[m - c:m + c, -c - c // 2:-c // 2].mean()])
    if np.abs(sides - means.mean()).min() < 30 or np.ptp(sides) > 50:
        return None                                   # a stripe or a picture between two margins: not round
    return {'hole': 0.3} if gray[r < 0.22].mean() < 0.45 * inner.mean() else {}


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


def line_share(gray: np.ndarray, vertical: bool) -> float:
    """How much of the fine detail runs unbroken across the whole image (0..1): about 0.8 for
    ribbed metal, below 0.2 for concrete or grass whose stains happen to repeat."""
    img = Image.fromarray(np.clip(gray, 0, 255).astype(np.uint8))
    fine = gray - np.asarray(img.filter(ImageFilter.GaussianBlur(8)), np.float32)
    return float(fine.mean(axis=0 if vertical else 1).var() / (fine.var() + 1e-6))


def auto_spec(key: str, img: Image.Image, modes: set[str]) -> dict | None:
    """A recipe for one borrowed texture. A shape (bricks, a door, a sign's words) is drawn only
    where the name or the picture clearly calls for it; anything unsure stays a plain surface in
    the original's colours, which reads as nothing in particular rather than as something wrong."""
    k = subject(key)
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
    natural = opts.pop('natural', False)
    name = key.lower().split('.')[-1]
    if gen == 'door' and 'door' not in name:
        gen, opts = None, {}                          # the Door group also holds shutters, belts, panels
    small = np.asarray(rgb.resize((64, 64)), np.float32)
    goods = small.reshape(-1, 3)[(small.max(axis=2) - small.min(axis=2)).ravel() > 110]   # strong colours
    if gen == 'machine' and len(goods) < 80:
        gen, opts = None, {}                          # the machine's plain side
    if std < 4 and small.reshape(-1, 3).std(axis=0).max() < 5 and 'additive' not in modes:
        gen, opts = 'flat', {'top': [int(c) for c in mid], 'bottom': [int(c) for c in mid]}   # one colour all over
    if 'masked' in modes and (np.asarray(img.convert('RGBA'))[..., 3] < 128).mean() < 0.005:
        modes = modes - {'masked'}                    # drawn masked but solid all over (the ball skin)
    if 'masked' in modes and gen != 'grate':
        return None                                   # see-through shapes (foliage, decals): later, by hand
    blob = gray[:12].mean() + gray[-12:].mean() + gray[:, :12].mean() + gray[:, -12:].mean() < gray[44:84, 44:84].mean()
    if 'additive' in modes and blob and gen not in ('glow', 'lamp'):
        gen, opts = 'glow', {}
    elif gen == 'glow' and not ('additive' in modes and blob):
        gen, opts = None, {}                          # water or a wall drawn added: no black backdrop
    disc = disc_shape(gray) if gen in (None, 'noise', 'tiles', 'panel', 'bricks') and 'additive' not in modes else None
    if disc is not None:
        yy, xx = np.mgrid[0:64, 0:64]
        face = np.hypot(xx - 31.5, yy - 31.5) < 26
        dark, mid, light = tones_of(small[face])
        back = np.concatenate([small[:6, :6], small[:6, -6:], small[-6:, :6], small[-6:, -6:]]).reshape(-1, 3).mean(axis=0)
        gen, opts = 'disc', {'back': [int(c) for c in back], **disc}
        if any(w in k for w in ('manhole', 'drain', 'cover')):
            opts['grid'] = 8
    nx, px, ny, py = periodicity(gray)
    ex = float(np.abs(np.diff(gray, axis=1)).mean())   # edges across x (vertical lines)
    ey = float(np.abs(np.diff(gray, axis=0)).mean())   # edges across y (horizontal lines)
    if not natural and gen in (None, 'noise', 'corrugated', 'panel') and py > 12 and ny >= 4 and 0.45 < ex / max(ey, 1e-3) < 1.1 and 'roof' not in k:
        gen = 'bricks'   # rows of courses with short joints between: a brick / block wall
    if not natural and gen in (None, 'noise', 'panel', 'corrugated', 'planks', 'tiles', 'bricks'):
        strong_x, strong_y = px > 12, py > 12
        if gen == 'bricks':
            opts.update(rows=int(min(16, max(4, ny))), cols=int(min(8, max(2, nx // 2 or 2))))
        elif gen == 'tiles' or (gen in (None, 'noise') and strong_x and strong_y):
            n = int(min(8, max(2, round((nx + ny) / 2))))
            if n > 2 or gen == 'tiles':
                gen = 'tiles'
                opts.update(n=n, grout=0.04)
            else:
                gen = 'noise'                         # a 2 x 2 "grid" is the image's own seams
        elif gen == 'planks' or (gen in (None, 'noise') and strong_y and not strong_x and 'wood' in k):
            gen = 'planks'
            opts.update(n=int(min(10, max(1, ny if strong_y else 4))), vertical=bool(strong_x and not strong_y))
        elif strong_x != strong_y and gen in (None, 'noise', 'panel', 'corrugated') and 'ridges' not in opts:
            n = nx if strong_x else ny
            gen = 'corrugated' if n >= 6 else 'siding' if n > 2 else 'noise'   # 1-2 stripes: stains, not boards
            opts = {'ridges': int(min(24, n)), 'vertical': bool(strong_x)} if gen == 'corrugated' else {'n': int(n), 'vertical': bool(strong_x)} if gen == 'siding' else {}
        elif gen is None:
            gen = 'noise'
    gen = gen or 'noise'
    if gen in ('corrugated', 'siding') and 'ridges' not in opts and max(line_share(gray, True), line_share(gray, False)) < 0.2:
        gen, opts = 'noise', {}                       # a repeat in the stains: stripes would be invented
    walls = any(w in k for w in ('wall', 'wal0', 'brick', 'brck', 'house', 'build', 'facade'))
    hole = window_rect(gray) if walls and gen in ('noise', 'bricks', 'tiles', 'siding', 'corrugated', 'panel') and 'additive' not in modes else None
    if hole:
        # a wall with a window in it: the wall from what's around the window, then the window
        x0, y0, x1, y1 = (int(round(v * 64)) for v in hole)
        wall = np.ones((64, 64), bool)
        wall[y0:y1, x0:x1] = False
        dark, mid, light = tones_of(small[wall])
        if gen in ('tiles', 'siding', 'corrugated', 'panel'):
            gen, opts = 'noise', {}                   # the "grid" was the window's own edges
        opts.update(window=hole, glass=[int(c) for c in np.median(small[y0:y1, x0:x1].reshape(-1, 3), axis=0)], sill=[int(c) for c in light])
    if gen == 'noise':
        opts.setdefault('contrast', round(float(np.clip(std / 32.0, 0.4, 1.6)), 2))
        opts.setdefault('beta', 1.8)
        dark_frac = float((gray < gray.mean() - 2.2 * std).mean()) if std > 1 else 0.0
        if dark_frac > 0.01 and not hole and float(mid.mean()) < 190:   # on snow or white paint specks read as dirt
            opts['spots'] = round(min(0.2, dark_frac * 4), 3)
    if gen == 'grate' and 'masked' in modes:
        opts['masked'] = True
    if gen == 'sign':
        hsv = np.asarray(rgb.convert('HSV').resize((32, 32)), np.float32).reshape(-1, 3)
        vivid = hsv[hsv[:, 1] > 80]
        base = np.asarray(rgb.resize((32, 32)), np.float32).reshape(-1, 3)
        col = base[hsv[:, 1] > 80].mean(axis=0) if len(vivid) > 20 else mid
        opts.update(color=[int(c) for c in col], mark=[255, 255, 255] if col.mean() < 150 else [30, 30, 30])
        if any(w in k for w in ('stripe', 'hazard', 'chevron')):
            opts['style'] = 'stripes'
        elif any(w in k for w in ('logo', 'emblem', 'flag', 'badge')):
            opts['style'] = 'emblem'
        else:
            opts.update(sign_layout(key, rgb, opts['color'], opts['mark']))
    if gen == 'door':
        edges = np.abs(np.diff(gray, axis=1)).mean(axis=0)
        opts['leaves'] = 2 if edges[61:66].max() > 3 * np.median(edges) else 1
        if gray[32:96, 16:112].mean() < 60 and (gray > 165).mean() > 0.04:
            opts['glass'] = True                      # dark panes in a light frame
    if gen == 'machine':
        pal = Image.fromarray(goods.astype(np.uint8)[None]).quantize(4).getpalette()[:12]
        opts['goods'] = [pal[i:i + 3] for i in range(0, 12, 3)]
    if gen == 'window':
        opts.update(nx=int(min(6, max(1, nx if px > 8 else 2))), ny=int(min(6, max(1, ny if py > 8 else 2))))
    spec.update(gen=gen, dark=[int(c) for c in dark], mid=[int(c) for c in mid], light=[int(c) for c in light])
    spec.update(opts)
    return spec


def sign_layout(key: str, rgb: Image.Image, color: list, mark: list) -> dict:
    """A sign's options beyond its colours: the panels on its sheet, and whether it hangs in a
    street (shop names) or on a work site (notices). The package says where."""
    k = key.lower()
    out: dict = {'panels': sign_panels(rgb) or [[0, 0, 1, 1, color, mark]]}
    if len(out['panels']) == 1:
        if any(w in k.split('.')[-1] for w in ('men', 'toilet', 'potty')):
            out['words'] = 'WC'
        elif any(w in k for w in ('billboard', 'park', 'marina', 'urban', 'pueblo', 'shop', 'store', 'mall', 'cafe', 'market')):
            out['shop'] = True
    return out


def with_gain(key: str, spec: dict, img: Image.Image) -> dict:
    """spec, with a `gain` if what it makes is off the original's brightness by over a quarter."""
    spec.pop('gain', None)
    if 'panels' in spec:
        return spec                                   # every panel has the original's own colours already
    want = float(np.asarray(img.convert('RGB').resize((32, 32)), np.float32).mean())
    got = float(np.asarray(make(key, spec).convert('RGB').resize((32, 32)), np.float32).mean())
    if got > 1 and abs(want / got - 1) > 0.25:
        spec['gain'] = round(float(np.clip(want / got, 0.3, 3.0)), 2)
    return spec


def needed_textures() -> dict[str, set[str]]:
    """Textures non-classic maps borrow from a pack they can't count on, with the modes they're
    drawn in: classic for every one of them, unverified too for community maps."""
    sys.path.insert(0, str(MFPORT))
    from mflib import config, packs
    cfg = config.load()
    p = packs.load()
    need: dict[str, set[str]] = {}
    for lj in (cfg.out_dir / 'Maps').glob('*/level.json'):
        pack = packs.map_pack(p, lj.parent.name)
        if pack == 'classic':
            continue
        borrowed = {'classic', 'unverified'} if pack == 'community' else {'classic'}
        for key, modes in json.loads(lj.read_text())['texture_usage'].items():
            pkg = key.split('.')[0]
            if set(modes) <= {'sky'} or pkg.lower() == 'engine' or packs.package_pack(p, pkg) not in borrowed:
                continue
            need.setdefault(key.lower(), set()).update(modes)
    return need


def asks_again(key: str, old: dict, spec: dict, img: Image.Image) -> bool:
    """--reclassify: whether an entry takes the new recipe. Only where the old one came from a
    rule since found wrong; the rest were looked at in game (some set by hand) and stay.
    A sign stays a sign and gets the original's layout."""
    if spec['gen'] == 'flat':
        return old['gen'] in ('sign', 'screen', 'grate')   # a colour swatch, whatever its group is called
    if old['gen'] == 'sign':
        if 'style' in old or 'panels' in old:
            return False
        new = {k: spec[k] for k in ('style',) if spec['gen'] == 'sign' and k in spec}
        new = new or sign_layout(key, img.convert('RGB'), old.get('color', old['mid']), old.get('mark', [255, 255, 255]))
        spec.clear()
        spec.update(old, **new)
        return True
    if spec['gen'] == 'door':
        return old['gen'] in ('panel', 'noise', 'screen')   # a striped one is a shutter: stays
    if spec['gen'] in ('disc', 'machine') or 'window' in spec:
        return True                                   # shapes there was no generator for
    if old['gen'] in ('screen', 'grate') and spec['gen'] != old['gen']:
        return True                                   # named after its package or group (Terminal walls, ...Mesh skins)
    natural = any(w in subject(key) for w in next(words for words, _, o in KEYWORDS if o.get('natural')))
    return natural and old['gen'] in ('bricks', 'tiles', 'siding', 'corrugated', 'planks') and spec['gen'] == 'noise'


def write_auto_table(reclassify: bool = False) -> None:
    hand = {k.lower() for k in tomllib.loads(TABLE.read_text())}
    # entries already there stay as they are: their art is imported and reviewed, and a map joining
    # the list must not change it for the others. --reclassify asks every one of them again (after
    # the rules here changed) and keeps those whose generator comes out the same, signs apart:
    # their layout is read from the original now
    kept = tomllib.loads(AUTO_TABLE.read_text()) if AUTO_TABLE.exists() else {}
    known = {} if reclassify else {k.lower() for k in kept}
    man = {k.lower(): (k, v) for k, v in json.loads((MFPORT / 'work' / 'out' / 'textures.json').read_text()).items()}
    lines = ['# Written by `lookalike.py --auto` (re-run after converting more core maps); edit an entry or',
             '# move it to lookalikes.toml to tune it. Colours were sampled from the originals.', '']
    skipped, changed = [], []
    need = needed_textures()
    specs = {k: v for k, v in kept.items() if k.lower() not in hand}
    for lk, modes in sorted(need.items()):
        if lk in hand or lk in known or lk not in man:
            continue
        key, info = man[lk]
        f = MFPORT / 'work' / 'out' / info['file']
        if not f.exists():
            continue
        img = Image.open(f)
        spec = auto_spec(key, img, modes)
        old = kept.get(key)
        if spec is None:
            if old is None:
                skipped.append(key)
            continue
        if old and not asks_again(key, old, spec, img):
            continue
        specs[key] = with_gain(key, spec, img)
        changed.append(f'{key}: {old["gen"] if old else "-"} -> {spec["gen"]}' + (' + window' if 'window' in spec else ''))
    for key, spec in sorted(specs.items(), key=lambda kv: kv[0].lower()):
        lines.append(f'["{key}"]')
        for name, val in spec.items():
            lines.append(f'{name} = {json.dumps(val)}')
        lines.append('')
    if skipped:
        lines.append('# not generated (see-through shapes; replace by hand): ' + ', '.join(skipped))
    AUTO_TABLE.write_text('\n'.join(lines) + '\n')
    report = HERE / 'preview' / 'auto_changes.txt'
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text('\n'.join(changed) + '\n')
    print(f'[lookalike] {len(specs)} automatic entries -> {AUTO_TABLE.name}: {len(changed)} new or changed ({report}), {len(skipped)} left for hand-made art')


def main(argv: list[str]) -> None:
    if '--auto' in argv:
        write_auto_table('--reclassify' in argv)
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
