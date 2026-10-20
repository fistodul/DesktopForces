"""Music: Mobile Forces .umx packages hold one tracker module each (Impulse Tracker 'it',
also s3m/xm/mod). Extracted straight from the package, rendered to WAV with ffmpeg's
libopenmpt decoder, imported as looping SoundWaves (<pack root>/Music/<pkg>/<name>, mflib/packs.py).

UMusic native data (v83): FName FileType, then a lazy array (INT skip offset, compact count, bytes).
Overrides: Tools/mfport/overrides/music/<package>.ogg|.wav replaces the original.
"""
from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

from .config import Config, TOOL_DIR
from .upkg import Package, Reader

OVERRIDE_DIR = TOOL_DIR / 'overrides' / 'music'


def extract(cfg: Config, package: str) -> list[Path]:
    src = cfg.game_dir / 'Music' / f'{package}.umx'
    pkg = Package(str(src))
    out_dir = cfg.raw_dir / 'Music' / package
    out_dir.mkdir(parents=True, exist_ok=True)
    files = []
    for e in pkg.exports_of('Music'):
        r = Reader(pkg.data, pkg.read_object(e).native_offset)
        file_type = pkg.names[r.index()]
        r.i32()                       # lazy array skip position
        n = r.index()
        dst = out_dir / f'{e.name}.{file_type}'
        dst.write_bytes(r.raw(n))
        files.append(dst)
    return files


def convert(cfg: Config, package: str, rate: int = 22050) -> list[Path]:
    """Render a music package to 16-bit stereo WAV in work/out/Music/<pkg>/."""
    out_dir = cfg.out_dir / 'Music' / package
    out_dir.mkdir(parents=True, exist_ok=True)
    results = []
    override = next((p for p in (OVERRIDE_DIR / f'{package}.ogg', OVERRIDE_DIR / f'{package}.wav') if p.exists()), None)
    sources = [override] if override else extract(cfg, package)
    for src in sources:
        dst = out_dir / f'{src.stem if not override else package}.wav'
        if not shutil.which('ffmpeg'):
            raise RuntimeError('ffmpeg (with libopenmpt) is needed to render tracker music')
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', str(src), '-ac', '2', '-ar', str(rate),
                        '-acodec', 'pcm_s16le', str(dst)], check=True)
        results.append(dst)
    print(f'[music] {package}: {", ".join(p.name for p in results)}')
    return results


def packages_used(level_json: dict) -> set[str]:
    out = set()
    for r in level_json.get('spawns', []):
        if r.get('type') == 'map_info' and r.get('song'):
            out.add(r['song'].split('.')[0])
    return out
