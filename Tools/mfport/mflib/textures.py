"""Texture export (UCC) and conversion (BMP -> PNG) with override support.

Keys: every texture is identified by its full UE1 path `package.group.name`
(e.g. `rage_warehouse.metal.ware_metal_29`), which is also what BSP surfaces and
static-mesh triangles reference.

Overrides: a file at `Tools/mfport/overrides/textures/<package>/<group>/<name>.png`
(any size) is replacement art for that texture. This is how the project moves away from
the original game's art: drop in legally clean textures there (they ARE committed to
git; Tools/artgen/lookalike.py makes procedural ones) and re-run the pipeline. Both are
imported; core maps use the replacement, classic maps the original (unless packs.toml
says remaster_classic). UV mapping always uses the original texture's size, so
replacements of any resolution line up.
"""
from __future__ import annotations

import io
import json
import struct
from pathlib import Path

from PIL import Image

from . import ucc
from .config import Config, TOOL_DIR
from .upkg import Package, Reader

OVERRIDE_DIR = TOOL_DIR / 'overrides' / 'textures'

# Textures the game code uses directly (HUD), converted even if no map references them.
UI_TEXTURES = {
    'rageweapons.weaponicons': {'masked', 'ui'},       # weapon icon sheet (RageWeapon.WeaponIcon rects)
    'deagle.icons.deagleicon': {'masked', 'ui'},       # MF-mods weapon icons
    'grenlaunch.skins.grenlaunchimage': {'masked', 'ui'},
    'predatormissile.icons.predatoricon': {'masked', 'ui'},
    'rageweapons.icons.scopemask': {'masked', 'ui'},
    'rageweapons.icons.sniperrifleaim': {'masked', 'ui'},
    'rageweapons.icons.rocketlauncheraim': {'masked', 'ui'},
    # RageHUD atlases (TexRects in RageHUD defaultproperties): item boxes per colour set,
    # translucent backs + ammo bars, health/armour/loadout icons per team, loadout screen
    # tint, pain flash, scoreboard bits
    'rage.boxes': {'masked', 'ui'},
    'rage.backs': {'masked', 'ui'},
    'rage.icons': {'masked', 'ui'},
    'rage.loadoutbackblue': {'ui'},
    'rage.loadoutbackred': {'ui'},
    'rage.owblue': {'ui'},
    'rage.owred': {'ui'},
    'rage.scoreboardbits': {'masked', 'ui'},
    'rage.scoreboxes': {'masked', 'ui'},         # RageTeamHUD.DrawScoreBar
    'rage.scoreicons': {'masked', 'ui'},
    'rage.pickupicons': {'masked', 'ui'},
    'rage.minimapnewicons': {'masked', 'ui'},
    # RageHUD.CrosshairGraphic(0..23) = CrossHairA..X; drawn subtractive + additive in MF, so
    # black means "nothing": alpha from brightness lets the HUD darken only the shape
    **{f'rage.crosshair{c}': {'lumalpha'} for c in 'abcdefghijklmnopqrstuvwx'},
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
            'export': e.index,
            'format': props.get('Format', 0),
            'palette': props.get('Palette'),
            'comp_format': props.get('CompFormat') if props.get('bHasComp') else None,
            'usize': props.get('USize', 0),
            'vsize': props.get('VSize', 0),
            'masked': bool(props.get('bMasked', False)),
            'polyflags': int(props.get('PolyFlags', 0) or 0),
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
    pkg = Package(str(_package_path(cfg, package)))
    raw = None
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
        needs_alpha = bool(modes & {'masked', 'lumalpha', 'fontalpha'}) or info['masked']
        override = OVERRIDE_DIR / rel.with_suffix('.png')
        replacement = None
        if override.exists():
            # replacement art: imported next to the original (/Game/MF/Replacements); core maps
            # use it, classic maps keep the original (packs.toml remaster_classic)
            replacement = cfg.out_dir / 'Replacements' / rel.with_suffix('.png')
            replacement.parent.mkdir(parents=True, exist_ok=True)
            rep = Image.open(override).convert('RGBA' if needs_alpha else 'RGB')
            if needs_alpha and rep.mode == 'RGBA':
                rep = bleed_alpha(rep)
            _limit_size(rep, cfg.max_texture_size).save(replacement, optimize=True)
        if (native := decode_p8(pkg, info, needs_alpha)) is not None:
            img = native
        else:
            raw = raw or export_package(cfg, package)
            src = raw / f'{info["name"]}.bmp'
            if not src.exists():
                print(f'  ! missing export for {key}')
                continue
            img = _load_bmp(src, needs_alpha)
        if 'lumalpha' in modes:
            rgb = img.convert('RGB')
            img = rgb.convert('RGBA')
            img.putalpha(Image.merge('RGB', rgb.split()).convert('L').point(lambda v: min(255, v * 2)))
        if 'fontalpha' in modes:
            # bitmap font page: white glyphs, anti-aliasing as coverage
            lum = img.convert('L')
            img = Image.new('RGBA', img.size, (255, 255, 255, 0))
            img.putalpha(lum)
        if needs_alpha and img.mode == 'RGBA' and not modes & {'ui', 'fontalpha', 'lumalpha'}:
            img = bleed_alpha(img)
        img = _limit_size(img, cfg.max_texture_size)
        img.save(dst, optimize=True)
        manifest[key] = {
            'file': str(dst.relative_to(cfg.out_dir)),
            'usize': info['usize'], 'vsize': info['vsize'],
            'alpha': needs_alpha, 'override': override.exists(),
            **({'replacement': str(replacement.relative_to(cfg.out_dir))} if replacement else {}),
            # HUD art / fonts / minimaps: drawn by the canvas, imported without mips
            'ui': bool(modes & {'ui', 'fontalpha', 'lumalpha'}),
        }
    return manifest


def bleed_alpha(img: Image.Image, passes: int = 32) -> Image.Image:
    """Give see-through texels the colour of their opaque neighbours (alpha unchanged).

    UE1 masked textures keep palette index 0 (often white or magenta) under the holes;
    bilinear filtering and mips blend that into the edges (white fringes on tyres, fences).
    """
    import numpy as np
    a = np.asarray(img, dtype=np.float32)
    rgb, alpha = a[..., :3].copy(), a[..., 3]
    known = alpha >= 128
    if known.all() or not known.any():
        return img
    for _ in range(passes):
        if known.all():
            break
        acc = np.zeros_like(rgb)
        cnt = np.zeros(known.shape, dtype=np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            k = np.roll(known, (dy, dx), axis=(0, 1))     # textures tile: wrap around
            acc += np.roll(rgb, (dy, dx), axis=(0, 1)) * k[..., None]
            cnt += k
        grow = ~known & (cnt > 0)
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        known = known | grow
    rgb[~known] = rgb[known].mean(axis=0)
    out = np.concatenate([rgb, alpha[..., None]], axis=-1).clip(0, 255).astype(np.uint8)
    return Image.fromarray(out, 'RGBA')


def _read_mips(r: Reader, version: int) -> list[tuple[int, int, bytes]]:
    """TArray<FMipmap>: (width, height, data) per level."""
    out = []
    for _ in range(r.index()):
        if version >= 63:
            r.u32()                # lazy array: offset after the data
        data = r.raw(r.index())
        w, h = r.u32(), r.u32()
        r.u8(), r.u8()             # UBits, VBits
        out.append((w, h, data))
    return out


_DXT_FOURCC = {3: b'DXT1', 7: b'DXT3', 8: b'DXT5'}


def _decode_dxt(w: int, h: int, data: bytes, fmt: int) -> Image.Image | None:
    """S3TC block data -> RGBA, through Pillow's DDS reader (a 128-byte header in front)."""
    fourcc = _DXT_FOURCC.get(fmt)
    if not fourcc:
        return None
    header = struct.pack('<4s7I44s2I4s5I5I', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000, h, w, len(data),
                         0, 1, b'\0' * 44, 32, 0x4, fourcc, 0, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0)
    img = Image.open(io.BytesIO(header + data))
    img.load()
    return img.convert('RGBA')


def decode_p8(pkg: Package, info: dict, alpha: bool) -> Image.Image | None:
    """Read a texture straight from the package, top mip.

    Rage textures are palettised (P8) and most also carry an S3TC copy (CompMips, bHasComp)
    which MF's Direct3D renderer drew: true colour, and on some posters the P8 copy is a
    magenta-keyed fallback. The S3TC copy wins, except over palettes with graded alpha:
    Rage's HUD art keeps soft alpha in the palette (MF draws it with STY_Alpha), DXT1 only
    has 1-bit alpha. A palette with one alpha value is opaque; masked P8 textures use index 0
    as the hole like UE1. None for formats this can't read.
    """
    if info.get('format', 0) != 0 or not info.get('palette'):
        return None
    pal_exp = pkg.find(info['palette'].split('.')[-1], 'Palette')
    if pal_exp is None:
        return None
    r = Reader(pkg.data, pkg.read_object(pal_exp).native_offset)
    colors = [tuple(r.raw(4)) for _ in range(r.index())]
    graded = len({c[3] for c in colors}) > 2
    exp = pkg.exports[info['export'] - 1]
    r = Reader(pkg.data, pkg.read_object(exp).native_offset)
    mips = _read_mips(r, pkg.version)
    if not mips:
        return None
    w, h, data = mips[0]
    if info.get('comp_format') is not None and not graded:
        comp = _read_mips(r, pkg.version)
        img = _decode_dxt(*comp[0], info['comp_format']) if comp else None
        # a masked use needs the holes: keep P8 when only its index 0 has them
        if img is not None and alpha and img.getextrema()[3][0] == 255 and 0 in data:
            img = None
        if img is not None:
            return img if alpha else img.convert('RGB')
    if w * h != len(data):
        return None
    pal = []
    for i, c in enumerate(colors[:256]):
        pal += [c[0], c[1], c[2], c[3] if graded else (0 if i == 0 and alpha else 255)]
    img = Image.frombytes('P', (w, h), data)
    img.putpalette(bytes(pal + [0] * (1024 - len(pal))), rawmode='RGBA')
    img = img.convert('RGBA')
    return img if alpha or graded else img.convert('RGB')


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
