"""Sound export (UCC) and conversion to 16-bit PCM WAV (UE only imports 16-bit).

Sounds are keyed `package.name` (UE1 sound groups are flattened by UCC's export, the
same way the game's script refers to them, e.g. Sound'WeaponSFX_Pistol.Fire').
Overrides: Tools/mfport/overrides/sounds/<package>/<name>.wav replaces the original.
"""
from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

from . import ucc
from .config import Config, TOOL_DIR

OVERRIDE_DIR = TOOL_DIR / 'overrides' / 'sounds'


def export_package(cfg: Config, package: str) -> Path:
    out = cfg.raw_dir / 'Sounds' / package
    if out.exists() and any(out.iterdir()):
        return out
    src = cfg.game_dir / 'Sounds' / f'{package}.uax'
    rel = src.relative_to(cfg.game_dir).as_posix() if src.exists() else f'System/{package}.u'
    ucc.batch_export(cfg, rel, 'Sound', 'wav', out)
    return out


def convert_package(cfg: Config, package: str) -> int:
    raw = export_package(cfg, package)
    dst_dir = cfg.out_dir / 'Sounds' / package
    dst_dir.mkdir(parents=True, exist_ok=True)
    n = 0
    for wav in sorted(raw.glob('*.wav')):
        dst = dst_dir / wav.name
        override = OVERRIDE_DIR / package / wav.name
        src = override if override.exists() else wav
        if shutil.which('ffmpeg'):
            subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', str(src), '-acodec', 'pcm_s16le', str(dst)], check=True)
        else:
            shutil.copy(src, dst)
        n += 1
    print(f'[sounds] {package}: {n}')
    return n


def packages_used(level_json: dict) -> set[str]:
    """Sound packages referenced by a converted level (ambient sounds)."""
    out = set()
    for r in level_json.get('spawns', []):
        if r.get('type') == 'ambient_sound' and r.get('sound'):
            out.add(r['sound'].split('.')[0])
    return out


# Packages the gameplay code refers to (weapon sounds etc.)
GAMEPLAY_PACKAGES = [
    'WeaponSFX_Knife', 'WeaponSFX_Pistol', 'WeaponSFX_MachineGun', 'WeaponSFX_Shotgun',
    'WeaponSFX_SniperRifle', 'WeaponSFX_HeavyMachineGun', 'WeaponSFX_RocketLauncher',
    'WeaponSFX_Grenades', 'WeaponSFX_TripBombs', 'WeaponSFX_AdrenalineShot',
    'MiscSFX', 'footstepSFX', 'RagePlayerSounds', 'Announcer',
    'deagleSFX', 'gren',   # MF-mods weapons
    'WallHitSFX', 'GibsSFX',   # bullet impacts (RageEffect.AddWallHit), gibs
    'BuggySFX', 'HumveeSFX', 'TruckSFX', 'ArmadilloSFX', 'TrailerSFX', 'WheelSFX', 'VehicleFragSFX',   # vehicles
    'boat', 'jeep',   # MF-mods boat (Gregaras), jeep (Marcin, Gregaras): engines, the jeep's gun
]
