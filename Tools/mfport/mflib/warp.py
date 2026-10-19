"""Local alignment of a remake map onto its original (Docs/WarehouseMerge.md).

The global similarity fit (vmf_maps.toml) leaves local errors of 1-2 m because the remake
was rebuilt by hand. Here both maps' walkable floors are rasterised in the shared frame;
for overlapping patches the shift that best matches the remake's floor layout to the
original's is searched; the shifts become a smooth offset field (Gaussian-weighted) that
moves every remake vertex, prop and light. Smooth means no new seams inside the remake.

Needs numpy (pipeline dev dependency for map merging only).
"""
from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np

from .mfmesh import read

CELL = 50.0         # cm
ZMAX = 1000.0       # cm: ignore roofs
PATCH = 2400.0      # cm window
STEP = 800.0        # cm between patch centres
MAX_SHIFT = 300.0   # cm search radius
SIGMA = 1200.0      # cm smoothing of the field


def floor_raster(map_dir: Path, x0: float, y0: float, w: int, h: int) -> np.ndarray:
    lvl = json.loads((map_dir / 'level.json').read_text())
    img = np.zeros((w, h), dtype=np.float32)
    for e in lvl['world']:
        if not e['mesh'].split('/')[-1].startswith('Collision'):
            continue
        mb = read(map_dir / f"{e['mesh']}.mfmesh.json")
        ox, oy, oz = e['location']
        for tris in mb.tris.values():
            for tri in tris:
                if tri[0][1][2] < 0.7:
                    continue
                p = np.array([[v[0][0] + ox, v[0][1] + oy, v[0][2] + oz] for v in tri])
                if p[:, 2].mean() > ZMAX:
                    continue
                _fill(img, (p[:, 0] - x0) / CELL, (p[:, 1] - y0) / CELL)
    return np.minimum(img, 1.0)


def _edges(img: np.ndarray) -> np.ndarray:
    """Cells where floor starts/stops, dilated by one cell."""
    e = np.zeros_like(img)
    e[1:, :] = np.maximum(e[1:, :], np.abs(img[1:, :] - img[:-1, :]))
    e[:, 1:] = np.maximum(e[:, 1:], np.abs(img[:, 1:] - img[:, :-1]))
    d = e.copy()
    d[1:, :] = np.maximum(d[1:, :], e[:-1, :])
    d[:-1, :] = np.maximum(d[:-1, :], e[1:, :])
    d[:, 1:] = np.maximum(d[:, 1:], e[:, :-1])
    d[:, :-1] = np.maximum(d[:, :-1], e[:, 1:])
    return d


def _fill(img, xs, ys):
    i0, i1 = max(0, int(math.floor(xs.min()))), min(img.shape[0] - 1, int(math.ceil(xs.max())))
    j0, j1 = max(0, int(math.floor(ys.min()))), min(img.shape[1] - 1, int(math.ceil(ys.max())))
    if i1 < i0 or j1 < j0:
        return
    gi, gj = np.meshgrid(np.arange(i0, i1 + 1) + 0.5, np.arange(j0, j1 + 1) + 0.5, indexing='ij')
    (ax, bx, cx), (ay, by, cy) = xs, ys
    det = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay)
    if abs(det) < 1e-9:
        return
    l1 = ((bx - gi) * (cy - gj) - (cx - gi) * (by - gj)) / det
    l2 = ((cx - gi) * (ay - gj) - (ax - gi) * (cy - gj)) / det
    inside = (l1 >= -0.01) & (l2 >= -0.01) & (1 - l1 - l2 >= -0.01)
    img[i0:i1 + 1, j0:j1 + 1] += inside


class OffsetField:
    def __init__(self, points: list[tuple[float, float, float, float, float]]):
        # (x, y, dx, dy, weight)
        self.p = np.array(points, dtype=np.float64) if points else np.zeros((0, 5))

    def __call__(self, x: float, y: float) -> tuple[float, float]:
        if not len(self.p):
            return 0.0, 0.0
        d2 = (self.p[:, 0] - x) ** 2 + (self.p[:, 1] - y) ** 2
        w = self.p[:, 4] * np.exp(-d2 / (2 * SIGMA * SIGMA))
        tw = w.sum()
        if tw < 1e-6:
            return 0.0, 0.0
        # fade to zero far from any control point (weight ~ confidence)
        fade = min(1.0, tw / 0.5)
        return float((w * self.p[:, 2]).sum() / tw * fade), float((w * self.p[:, 3]).sum() / tw * fade)

    def to_json(self):
        return self.p.tolist()


def estimate(base_dir: Path, remake_dir: Path, report: Path | None = None) -> OffsetField:
    lv = [json.loads((d / 'level.json').read_text()) for d in (base_dir, remake_dir)]
    pts = [w['location'] for l in lv for w in l['world']]
    x0 = min(p[0] for p in pts) - 6000
    y0 = min(p[1] for p in pts) - 6000
    x1 = max(p[0] for p in pts) + 6000
    y1 = max(p[1] for p in pts) + 6000
    w, h = int((x1 - x0) / CELL), int((y1 - y0) / CELL)
    # match floor outlines (walls, ledges): open floor matches anywhere, edges don't
    A = _edges(floor_raster(base_dir, x0, y0, w, h))
    B = _edges(floor_raster(remake_dir, x0, y0, w, h))
    half = int(PATCH / CELL / 2)
    rs = int(MAX_SHIFT / CELL)
    points = []
    for cx in np.arange(x0 + PATCH, x1 - PATCH, STEP):
        for cy in np.arange(y0 + PATCH, y1 - PATCH, STEP):
            i, j = int((cx - x0) / CELL), int((cy - y0) / CELL)
            a = A[i - half:i + half, j - half:j + half]
            if a.sum() < 60:
                continue   # no outlines to lock onto
            best, best_s, zero = -1.0, (0, 0), 0.0
            for di in range(-rs, rs + 1):
                for dj in range(-rs, rs + 1):
                    b = B[i - half - di:i + half - di, j - half - dj:j + half - dj]
                    if b.shape != a.shape:
                        continue
                    # normalised overlap of outlines: remake shifted by (di, dj) vs original
                    s = float((a * b).sum()) / (math.sqrt(float(a.sum()) * float(b.sum())) or 1.0)
                    if di == 0 and dj == 0:
                        zero = s
                    if s > best:
                        best, best_s = s, (di, dj)
            gain = best - zero
            at_border = abs(best_s[0]) == rs or abs(best_s[1]) == rs
            if best < 0.3 or gain < 0.03 or at_border:
                if zero >= 0.3:
                    points.append((cx, cy, 0.0, 0.0, 0.3))   # good as is: hold offset 0 here
                continue
            points.append((cx, cy, best_s[0] * CELL, best_s[1] * CELL, min(1.0, best)))
    field = OffsetField(points)
    if report:
        report.write_text(json.dumps({'points': field.to_json()}, indent=1))
    return field
