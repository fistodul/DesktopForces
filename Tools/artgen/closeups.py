#!/usr/bin/env python3
"""Which generated wall and floor textures deserve 1024 px: Tools/mfport/detail.toml, `textures`.

Reads the converted world geometry of the non-classic maps, measures how much of each map a
texture covers, and takes each map's biggest surfaces until they cover `--cover` of it, leaving
out what is too thin to matter in any map. Run again after converting more maps; it prints the
list to paste, it doesn't edit detail.toml (entries there are reviewed art).

    python3 Tools/artgen/closeups.py [--cover 0.5] [--min 0.04]
"""
from __future__ import annotations

import json
import sys
import tomllib
from collections import defaultdict
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
MFPORT = HERE.parent / 'mfport'
sys.path.insert(0, str(MFPORT))
from mflib import config, packs  # noqa: E402

# what has grain to show: flat colours, signs and lamps look the same at any size
SURFACES = {'noise', 'bricks', 'tiles', 'planks', 'plywood', 'panel', 'corrugated', 'siding', 'treadplate', 'grate'}
FLOATS = {'pos3f_nrm3f_uv2f': 8, 'pos3f_nrm3f_uv2f_col3f': 11}


def map_areas(level_dir: Path) -> dict[str, float]:
    """Material key -> visible surface in square metres."""
    out: dict[str, float] = defaultdict(float)
    for meta in (level_dir / 'geo').glob('World_*.mfmesh.json'):
        mesh = json.loads(meta.read_text())
        if not mesh.get('render', True):
            continue
        size = FLOATS[mesh['vertex_layout']]
        raw = meta.with_name(mesh['bin'])
        pos = np.fromfile(raw, '<f4', mesh['vertex_count'] * size).reshape(-1, size)[:, :3]
        index = np.fromfile(raw, '<u4', offset=mesh['vertex_count'] * size * 4)
        for sec in mesh['sections']:
            tri = index[sec['first_index']:sec['first_index'] + sec['num_triangles'] * 3].reshape(-1, 3)
            a, b, c = pos[tri[:, 0]], pos[tri[:, 1]], pos[tri[:, 2]]
            area = 0.5 * np.linalg.norm(np.cross(b - a, c - a), axis=1).sum() / 1e4
            out[mesh['materials'][sec['material']].split('|')[0].lower()] += float(area)
    return out


def pick(cover: float, least: float) -> list[str]:
    cfg = config.load()
    p = packs.load()
    specs = {}
    for name in ('lookalikes.toml', 'lookalikes_auto.toml'):
        specs.update({k.lower(): (k, v) for k, v in tomllib.loads((HERE / name).read_text()).items()})
    chosen: set[str] = set()
    for level in (cfg.out_dir / 'Maps').glob('*/level.json'):
        if packs.map_pack(p, level.parent.name) == 'classic':
            continue
        areas = map_areas(level.parent)
        total = sum(areas.values()) or 1.0
        covered = 0.0
        for key, area in sorted(areas.items(), key=lambda kv: -kv[1]):
            if covered >= cover or area / total < least:
                break
            covered += area / total
            spec = specs.get(key)
            if spec and spec[1]['gen'] in SURFACES and min(spec[1].get('size', [256])) >= 128:
                chosen.add(spec[0])
    return sorted(chosen, key=str.lower)


def main(argv: list[str]) -> None:
    def option(name: str, default: float) -> float:
        return float(argv[argv.index(name) + 1]) if name in argv else default
    keys = pick(option('--cover', 0.5), option('--min', 0.04))
    print(f'# {len(keys)} textures', file=sys.stderr)
    for key in keys:
        print(f'    "{key}",')


if __name__ == '__main__':
    main(sys.argv[1:])
