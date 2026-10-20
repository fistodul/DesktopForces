"""Mobile Forces bitmap fonts (Textures/MobileForceFonts.utx) -> fonts.json + page textures.

UE1 UFont native data: Pages (texture + per-character StartU/StartV/USize/VSize) and
CharactersPerPage; character code c is on page c / CharactersPerPage. The importer builds
offline-cache UFonts from it, so the HUD draws text with MF's own glyphs and metrics.
"""
from __future__ import annotations

import json
from pathlib import Path

from .config import Config
from .upkg import Package, Reader

PACKAGE = 'MobileForceFonts'


def read_fonts(cfg: Config) -> dict[str, dict]:
    pkg = Package(str(cfg.game_dir / 'Textures' / f'{PACKAGE}.utx'))
    out = {}
    for e in pkg.exports_of('Font'):
        name = pkg.ref_path(e.index)
        r = Reader(pkg.data, pkg.read_object(e).native_offset)
        pages = []
        for _ in range(r.index()):
            tex = pkg.ref_path(r.index())
            chars = [[r.i32(), r.i32(), r.i32(), r.i32()] for _ in range(r.index())]
            pages.append({'texture': f'{PACKAGE}.{tex}', 'chars': chars})
        out[name] = {'pages': pages, 'chars_per_page': r.i32()}
    return out


def texture_usage(cfg: Config) -> dict[str, set]:
    """Page textures for the textures step: white glyphs, coverage in alpha."""
    return {p['texture'].lower(): {'fontalpha'} for f in read_fonts(cfg).values() for p in f['pages']}


def write(cfg: Config) -> Path:
    fonts = read_fonts(cfg)
    path = cfg.out_dir / 'fonts.json'
    path.write_text(json.dumps(fonts))
    print(f'  fonts: {len(fonts)} -> {path}')
    return path
