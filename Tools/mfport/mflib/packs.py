"""Content packs: community (agreed authors, offered with Desktop Forces), classic (Mobile Forces Classic plugin) or
unverified (Mobile Forces Unverified plugin: community content not vouched for yet).

Decisions live in Tools/mfport/packs.toml (see the rules there). This module applies them and
writes work/out/packs.json, which the UE importer reads to put every asset under the right
root:

  classic  /MobileForcesClassic/Textures|Materials|Sounds|Music|Fonts|Props/...,
           /MobileForcesClassic/Maps/<map> (+ <map>_Data)
  community /Game/MF/Community/Textures|Materials|Sounds|Music/..., /Game/Maps/<map> (+ <map>_Data)
  replaced /Game/MF/Replacements/Textures|Materials/... (own art from overrides/textures)
  unverified /MobileForcesUnverified/Textures|Materials|Sounds|Music|Props/...,
           /MobileForcesUnverified/Maps/<map> (+ <map>_Data): whatever isn't retail or community
"""
from __future__ import annotations

import json
import re
import textwrap
import tomllib
from collections import Counter, defaultdict
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
    community = data.get('community', {})
    maps = {m.lower(): m for m in community.get('maps', {}) if m.lower() not in RETAIL_PACKAGES}
    packages = {p.lower() for p in community.get('packages', {}) if p.lower() not in RETAIL_PACKAGES}
    unverified = data.get('unverified', {})
    keep = {p.lower() for p in data.get('classic', {}).get('packages', [])}
    return {
        'unverified_root': unverified.get('root', '/MobileForcesUnverified'),
        'unverified_packages': {p.lower() for p in unverified.get('packages', []) if p.lower() not in RETAIL_PACKAGES},
        'classic_root': data.get('classic_root', '/MobileForcesClassic'),
        'community_root': data.get('community_root', '/Game/MF/Community'),
        'community_maps_root': data.get('community_maps_root', '/Game/Maps'),
        'remaster_classic': bool(data.get('remaster_classic', False)),
        'community_maps': maps,
        'community_packages': packages | set(maps),   # a community map's own embedded textures
        'classic_packages': RETAIL_PACKAGES | (keep - packages),
    }


def map_pack(packs: dict, name: str) -> str:
    if name.lower() in packs['community_maps']:
        return 'community'
    return 'classic' if name.lower() in packs['classic_packages'] else 'unverified'


def package_pack(packs: dict, package: str) -> str:
    if package.lower() in packs['community_packages']:
        return 'community'
    return 'classic' if package.lower() in packs['classic_packages'] else 'unverified'


def write_manifest(cfg: Config) -> Path:
    packs = load()
    maps = sorted(p.parent.name for p in (cfg.out_dir / 'Maps').glob('*/level.json'))
    replaced = []
    tex = cfg.out_dir / 'textures.json'
    if tex.exists():
        replaced = sorted(k.lower() for k, v in json.loads(tex.read_text()).items() if v.get('override'))
    out = {
        'classic_root': packs['classic_root'],
        'community_root': packs['community_root'],
        'community_maps_root': packs['community_maps_root'],
        'replaced_root': '/Game/MF/Replacements',
        'remaster_classic': packs['remaster_classic'],
        'maps': {m: map_pack(packs, m) for m in maps},
        'community_packages': sorted(packs['community_packages']),
        'unverified_root': packs['unverified_root'],
        'unverified_packages': sorted(packs['unverified_packages']),
        # the importer's rule: community_packages -> community, these -> classic, anything else -> unverified
        'classic_packages': sorted(packs['classic_packages']),
        'replaced': replaced,
    }
    path = cfg.out_dir / 'packs.json'
    path.write_text(json.dumps(out, indent=1))
    return path


def report(cfg: Config) -> None:
    """Per converted map: its pack, and for community / unverified maps the classic textures they still use."""
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
        print(f'{name:28s} {pack}, {len(usage)} textures, {total} still from the original game'
              + (': ' + ', '.join(f'{p} {len(v)}' for p, v in sorted(need.items())) if total else ''))


def cooked_report(project: Path) -> None:
    """What the last cook of each platform put in each pak, from the cooker's own lists
    (Saved/Cooked/<Platform>/<Project>/Metadata/ChunkManifest/pakchunkN.txt): files per folder
    and the maps."""
    for lists in sorted(project.glob('Saved/Cooked/*/*/Metadata/ChunkManifest')):
        platform, game = lists.parents[2].name, lists.parents[1].name
        print(f'\nLast {platform} cook ({lists}):')
        chunks = [f for f in lists.glob('pakchunk*.txt') if re.fullmatch(r'pakchunk\d+', f.stem)]
        for f in sorted(chunks, key=lambda f: int(f.stem[8:])):
            folders, maps = Counter(), []
            for line in f.read_text(errors='replace').splitlines():
                path = line.split('\t')[0].replace('\\', '/')
                parts = path.split(f'/{platform}/', 1)[-1].split('/')
                if parts[0] == game:
                    parts = parts[1:]
                if parts[0] == 'Plugins' and len(parts) > 3:
                    key = f'{parts[1]}/{parts[3]}'   # <plugin>/<folder>
                elif parts[0] == 'Content' and len(parts) > 3 and parts[1] == 'MF':
                    key = f'MF/{parts[2]}'
                elif parts[0] == 'Content' and len(parts) > 2:
                    key = parts[1]
                else:
                    key = parts[0]                 # Engine, loose files
                folders[key] += 1
                if len(parts) > 1 and parts[-2] == 'Maps' and not parts[-1].endswith('_BuiltData') and not parts[-1].startswith('PAL_'):
                    maps.append(parts[-1])
            top = ', '.join(f'{k} {n}' for k, n in folders.most_common(6))
            print(f'  {f.stem}: {sum(folders.values())} files ({top})')
            if maps:
                print(textwrap.fill(f'maps ({len(maps)}): ' + ', '.join(sorted(maps, key=str.lower)), 100,
                                    initial_indent='    ', subsequent_indent='      '))
