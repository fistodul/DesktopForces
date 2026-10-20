"""Pipeline configuration.

Values come from `Tools/mfport/mfport.toml` (copy `mfport.example.toml`), and any
key can be overridden with an environment variable `MFPORT_<KEY>` (upper case).
"""
from __future__ import annotations

import os
import tomllib
from dataclasses import dataclass
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent.parent          # Tools/mfport
PROJECT_DIR = TOOL_DIR.parent.parent                        # UE project root

# One Mobile Forces / UE1 unit in UE5 centimetres.
# MF's player capsule (radius 17, half-height 39) * 2.25 = 38 x 88 = the UE5
# default character capsule, so stock UE5 animation/camera assumptions fit.
# Every distance, speed and acceleration converted from MF data is multiplied by
# this; gravity too (-950 uu/s^2 -> -2137.5 cm/s^2).
UU_TO_CM = 2.25


@dataclass
class Config:
    game_dir: Path
    work_dir: Path
    ue_root: Path
    uproject: Path
    wine: str
    max_texture_size: int

    @property
    def raw_dir(self) -> Path:
        return self.work_dir / 'raw'

    @property
    def out_dir(self) -> Path:
        return self.work_dir / 'out'


def load() -> Config:
    data: dict = {}
    for name in ('mfport.toml', 'mfport.example.toml'):
        f = TOOL_DIR / name
        if f.exists():
            data = tomllib.loads(f.read_text())
            break

    def get(key: str, default=None):
        return os.environ.get(f'MFPORT_{key.upper()}', data.get(key, default))

    def path(key: str, default: str) -> Path:
        p = Path(os.path.expanduser(str(get(key, default))))
        return p if p.is_absolute() else (TOOL_DIR / p).resolve()

    return Config(
        game_dir=path('game_dir', '../../../MoFo/MobileForces'),
        work_dir=path('work_dir', 'work'),
        ue_root=path('ue_root', '../../../Linux_Unreal_Engine_5.8.3'),
        uproject=path('uproject', str(PROJECT_DIR / 'DesktopForces.uproject')),
        wine=str(get('wine', 'wine')),
        max_texture_size=int(get('max_texture_size', 512)),
    )
