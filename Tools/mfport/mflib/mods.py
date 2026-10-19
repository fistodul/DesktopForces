"""Mod plugins (Plugins/Mods/*): convert each plugin's own SourceArt for import.

Mod art is the mod author's (e.g. MF-mods, MIT), so it lives in the repo next to the code:
  Plugins/Mods/<Plugin>/SourceArt/<path>.bmp|png|wav
becomes work/out/Plugins/<Plugin>/<path>.png|wav and is imported to /<Plugin>/Generated/<path>.
8-bit BMPs follow the UE1 convention: palette index 0 is transparent.
"""
from __future__ import annotations

import shutil
from pathlib import Path

from PIL import Image

from .config import Config, PROJECT_DIR


def plugin_dirs() -> list[Path]:
    return sorted(p for p in (PROJECT_DIR / 'Plugins' / 'Mods').glob('*') if (p / 'SourceArt').is_dir())


def convert(cfg: Config) -> int:
    n = 0
    for plugin in plugin_dirs():
        src_root = plugin / 'SourceArt'
        dst_root = cfg.out_dir / 'Plugins' / plugin.name
        for src in src_root.rglob('*'):
            if not src.is_file():
                continue
            rel = src.relative_to(src_root)
            ext = src.suffix.lower()
            if ext in ('.bmp', '.png', '.pcx', '.tga'):
                dst = (dst_root / rel).with_suffix('.png')
                dst.parent.mkdir(parents=True, exist_ok=True)
                img = Image.open(src)
                if img.mode == 'P':
                    mask = img.point(lambda i: 0 if i == 0 else 255, mode='L')
                    img = img.convert('RGBA')
                    img.putalpha(mask)
                img.save(dst)
                n += 1
            elif ext == '.wav':
                dst = dst_root / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy(src, dst)
                n += 1
        print(f'[mods] {plugin.name}: SourceArt converted')
    return n
