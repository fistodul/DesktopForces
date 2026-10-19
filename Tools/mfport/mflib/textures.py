"""Texture export (UCC) and conversion (BMP -> PNG) with override support.

Keys: every texture is identified by its full UE1 path `package.group.name`
(e.g. `rage_warehouse.metal.ware_metal_29`), which is also what BSP surfaces and
static-mesh triangles reference.

Overrides: a file at `Tools/mfport/overrides/textures/<package>/<group>/<name>.png`
(any size) replaces the original texture. This is how the project moves away from
the original game's art: drop in legally clean / remastered textures there (they
ARE committed to git) and re-run the pipeline. UV mapping always uses the
original texture's size, so replacements of any resolution line up.
"""
from __future__ import annotations

import json
from pathlib import Path

from PIL import Image

from . import ucc
from .config import Config, TOOL_DIR
from .upkg import Package

OVERRIDE_DIR = TOOL_DIR / 'overrides' / 'textures'

# Textures the game code uses directly (HUD), converted even if no map references them.
UI_TEXTURES = {
    'rageweapons.weaponicons': {'masked'},       # weapon icon sheet (RageWeapon.WeaponIcon rects)
    'rageweapons.icons.scopemask': {'masked'},
    'rageweapons.icons.sniperrifleaim': {'masked'},
    'rageweapons.icons.rocketlauncheraim': {'masked'},
}


def texture_index(cfg: Config, package: str) -> dict[str, dict]:
    """key -> {name, group, usize, vsize} for every Texture in Textures/<package>.utx (or a map/.u file)."""
    path = _package_path(cfg, package)
    pkg = Package(str(path))
    out = {}
    for e in pkg.exports_of('Texture'):
        props = pkg.read_object(e).props
        key = f'{package}.{pkg.ref_path(e.index)}'   # exports' paths omit the package itself
        out[key.lower()] = {
            'key': key,
            'name': e.name,
            'usize': props.get('USize', 0),
            'vsize': props.get('VSize', 0),
            'masked': bool(props.get('bMasked', False)),
            'translucent': bool(props.get('bTransparent', False)),
        }
    return out


def _package_path(cfg: Config, package: str) -> Path:
    """Find a package file; UE1 names are case-insensitive, the file system may not be."""
    want = package.lower()
    for sub, ext in (('Textures', '.utx'), ('System', '.u'), ('Maps', '.umf')):
        folder = cfg.game_dir / sub
        p = folder / f'{package}{ext}'
        if p.exists():
            return p
        for f in folder.glob(f'*{ext}'):
            if f.stem.lower() == want:
                return f
    raise FileNotFoundError(f'texture package {package} not found under {cfg.game_dir}')


def export_package(cfg: Config, package: str) -> Path:
    """UCC-export all textures of a package to work/raw/Textures/<package>/ (flat, by name)."""
    out = cfg.raw_dir / 'Textures' / package
    if out.exists() and any(out.iterdir()):
        return out
    rel = _package_path(cfg, package).relative_to(cfg.game_dir).as_posix()
    ucc.batch_export(cfg, rel, 'Texture', 'bmp', out)
    return out


def convert(cfg: Config, package: str, usage: dict[str, set[str]]) -> dict[str, dict]:
    """Convert a package's textures that are used (key -> set of blend modes) to PNG.

    Returns manifest entries key -> {file, usize, vsize, alpha, override}.
    """
    package = _package_path(cfg, package).stem   # real spelling
    index = texture_index(cfg, package)
    raw = export_package(cfg, package)
    manifest = {}
    for lkey, info in index.items():
        modes = usage.get(lkey)
        if modes is None:
            continue
        key = info['key']
        parts = key.split('.')
        rel = Path(*parts)          # package/group/name
        dst = cfg.out_dir / 'Textures' / rel.with_suffix('.png')
        dst.parent.mkdir(parents=True, exist_ok=True)
        needs_alpha = bool(modes & {'masked'}) or info['masked']
        override = OVERRIDE_DIR / rel.with_suffix('.png')
        if override.exists():
            img = Image.open(override).convert('RGBA' if needs_alpha else 'RGB')
        else:
            src = raw / f'{info["name"]}.bmp'
            if not src.exists():
                print(f'  ! missing export for {key}')
                continue
            img = _load_bmp(src, needs_alpha)
        img = _limit_size(img, cfg.max_texture_size)
        img.save(dst, optimize=True)
        manifest[key] = {
            'file': str(dst.relative_to(cfg.out_dir)),
            'usize': info['usize'], 'vsize': info['vsize'],
            'alpha': needs_alpha, 'override': override.exists(),
        }
    return manifest


def _load_bmp(path: Path, alpha: bool) -> Image.Image:
    img = Image.open(path)
    if img.mode == 'P' and alpha:
        # UE1 masked textures: palette index 0 is transparent
        idx = img.copy()
        rgba = img.convert('RGBA')
        mask = idx.point(lambda i: 0 if i == 0 else 255, mode='L')
        rgba.putalpha(mask)
        return rgba
    return img.convert('RGB')


def _limit_size(img: Image.Image, max_size: int) -> Image.Image:
    w, h = img.size
    scale = max(w, h) / max_size
    if scale <= 1:
        return img
    return img.resize((max(1, round(w / scale)), max(1, round(h / scale))), Image.LANCZOS)


def write_manifest(cfg: Config, manifest: dict) -> Path:
    path = cfg.out_dir / 'textures.json'
    old = json.loads(path.read_text()) if path.exists() else {}
    old.update(manifest)
    path.write_text(json.dumps(old, indent=1, sort_keys=True))
    return path
