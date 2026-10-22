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
    # a .uax in Sounds (any case: Glock.uax holds 'glock.*'), else sounds inside a code package
    src = next((f for f in (cfg.game_dir / 'Sounds').glob('*.uax') if f.stem.lower() == package.lower()), None)
    rel = src.relative_to(cfg.game_dir).as_posix() if src else f'System/{package}.u'
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
    """Sound packages referenced by a converted level (ambient and periodic sounds)."""
    out = set()
    for r in level_json.get('spawns', []):
        if r.get('type') in ('ambient_sound', 'periodic_sound') and r.get('sound'):
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
    'heli',           # MF-mods heli (Marcin, Gregaras): engine loop, cockpit glass
    'parachute',      # Gregaras' parachute (CC0)
    # the servers' weapon mods (unverified pack: packs.toml [unverified])
    'AK74', 'm4a1', 'SC20K', 'MAC10', 'M24', 'm871', 'Desert', 'famas', 'g36', 'Glock', 'ultimax',
    'wa2000', 'M18', 'improvedRL',
]
