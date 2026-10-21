"""Content packs: core (part of Desktop Forces), classic (Mobile Forces Classic plugin) or
unverified (Mobile Forces Unverified plugin: community content not vouched for yet).

Decisions live in Tools/mfport/packs.toml (see the rules there). This module applies them and
writes work/out/packs.json, which the UE importer reads to put every asset under the right
root:

  classic  /MobileForcesClassic/Textures|Materials|Sounds|Music|Fonts|Props/...,
           /MobileForcesClassic/Maps/<map> (+ <map>_Data)
  core     /Game/MF/Community/Textures|Materials|Sounds|Music/..., /Game/Maps/<map> (+ <map>_Data)
  replaced /Game/MF/Replacements/Textures|Materials/... (own art from overrides/textures)
  unverified /MobileForcesUnverified/Textures|Sounds|Props/... ([unverified] packages)
"""
from __future__ import annotations

import json
import tomllib
from collections import defaultdict
from pathlib import Path

from .config import Config, TOOL_DIR

PACKS_FILE = TOOL_DIR / 'packs.toml'

# Everything the retail game installs (the MF community's vars.yml defaultFiles): always classic.
RETAIL_PACKAGES = {p.lower() for p in (
    # textures
    'car_park', 'Dam', 'DemoScreens', 'icebase', 'MobileForceFonts', 'rage_airport', 'rage_dock',
    'rage_generic', 'rage_lumberyard', 'rage_marina', 'rage_pueblo', 'RagePlayerGfx', 'RageRobotGfx',
    'RageScreenShots', 'rage_snow', 'rage_urban', 'rage_warehouse', 'Steve-DM1', 'train',
    # code packages (their embedded textures, meshes, sounds)
    'Core', 'Engine', 'Rage', 'RageGame', 'RageWeapons', 'RageGfx', 'RageEffects', 'RageMenu', 'Fire',
    'UWindow', 'UBrowser', 'RageBrowser', 'IpDrv', 'IpServer', 'Editor', 'EffectsFix',
    # sounds
    'Announcer', 'ArmadilloSFX', 'BuggySFX', 'CTFSFX', 'DetSFX', 'DominationSFX', 'footstepSFX', 'GibsSFX',
    'HumveeSFX', 'levelSFX', 'MiscSFX', 'oneshotSFX', 'RageMenuSFX', 'RagePlayerSounds', 'RagePlayerVoice',
    'SafeCrackerSFX', 'TrailerSFX', 'TruckSFX', 'VehicleFragSFX', 'WallHitSFX', 'WeaponSFX_AdrenalineShot',
    'WeaponSFX_Grenades', 'WeaponSFX_HeavyMachineGun', 'WeaponSFX_Knife', 'WeaponSFX_MachineGun',
    'WeaponSFX_Pistol', 'WeaponSFX_RocketLauncher', 'WeaponSFX_Shotgun', 'WeaponSFX_SniperRifle',
    'WeaponSFX_TripBombs', 'WheelSFX',
    # music
    'Ambient01', 'Ambient02', 'Ambient03', 'Frontend', 'Victory',
    # maps
    'entry', 'intro', 'mf-airport', 'mf-Carpark', 'mf-dockyard', 'mf-Ghetto', 'mf-Hydroworks', 'mf-Polar',
    'mf-Rail_Quarry', 'mf-sawmill', 'mf-warehouse', 'mf-waterfront', 'mf-western',
)}


def load() -> dict:
    data = tomllib.loads(PACKS_FILE.read_text())
    core = data.get('core', {})
    maps = {m.lower(): m for m in core.get('maps', {}) if m.lower() not in RETAIL_PACKAGES}
    packages = {p.lower() for p in core.get('packages', {}) if p.lower() not in RETAIL_PACKAGES}
    unverified = data.get('unverified', {})
    return {
        'unverified_root': unverified.get('root', '/MobileForcesUnverified'),
        'unverified_packages': {p.lower() for p in unverified.get('packages', []) if p.lower() not in RETAIL_PACKAGES},
        'classic_root': data.get('classic_root', '/MobileForcesClassic'),
        'core_root': data.get('core_root', '/Game/MF/Community'),
        'core_maps_root': data.get('core_maps_root', '/Game/Maps'),
        'remaster_classic': bool(data.get('remaster_classic', False)),
        'core_maps': maps,
        'core_packages': packages | set(maps),   # a core map's own embedded textures
    }


def map_pack(packs: dict, name: str) -> str:
    return 'core' if name.lower() in packs['core_maps'] else 'classic'


def package_pack(packs: dict, package: str) -> str:
    if package.lower() in packs['core_packages']:
        return 'core'
    return 'unverified' if package.lower() in packs['unverified_packages'] else 'classic'


def write_manifest(cfg: Config) -> Path:
    packs = load()
    maps = sorted(p.parent.name for p in (cfg.out_dir / 'Maps').glob('*/level.json'))
    replaced = []
    tex = cfg.out_dir / 'textures.json'
    if tex.exists():
        replaced = sorted(k.lower() for k, v in json.loads(tex.read_text()).items() if v.get('override'))
    out = {
        'classic_root': packs['classic_root'],
        'core_root': packs['core_root'],
        'core_maps_root': packs['core_maps_root'],
        'replaced_root': '/Game/MF/Replacements',
        'remaster_classic': packs['remaster_classic'],
        'maps': {m: map_pack(packs, m) for m in maps},
        'core_packages': sorted(packs['core_packages']),
        'unverified_root': packs['unverified_root'],
        'unverified_packages': sorted(packs['unverified_packages']),
        'replaced': replaced,
    }
    path = cfg.out_dir / 'packs.json'
    path.write_text(json.dumps(out, indent=1))
    return path


def report(cfg: Config) -> None:
    """Per converted map: its pack, and for core maps the classic textures it still uses."""
    packs = load()
    replaced = set()
    tex = cfg.out_dir / 'textures.json'
    if tex.exists():
        replaced = {k.lower() for k, v in json.loads(tex.read_text()).items() if v.get('override')}
    for lj in sorted((cfg.out_dir / 'Maps').glob('*/level.json')):
        name = lj.parent.name
        usage = json.loads(lj.read_text()).get('texture_usage', {})
        pack = map_pack(packs, name)
        if pack == 'classic':
            print(f'{name:28s} classic')
            continue
        need = defaultdict(list)
        for key, modes in usage.items():
            if set(modes) <= {'sky'}:
                continue   # sky windows show the sky zone, never their texture
            pkg = key.split('.')[0]
            if key.lower() not in replaced and package_pack(packs, pkg) == 'classic' and pkg.lower() != 'engine':
                need[pkg].append(key)
        total = sum(len(v) for v in need.values())
        print(f'{name:28s} core, {len(usage)} textures, {total} still from the original game'
              + (': ' + ', '.join(f'{p} {len(v)}' for p, v in sorted(need.items())) if total else ''))
