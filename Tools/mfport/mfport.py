#!/usr/bin/env python3
"""mfport - Mobile Forces asset pipeline (your original game install -> UE5 project).

Steps (run alone or chained with `all`):
  map <name...>        convert maps: BSP world geometry, static meshes, actors (UCC T3D export)
  textures             export + convert every texture the converted maps use
  sounds [pkg...]      export + convert sounds (default: gameplay packages + map ambience)
  music [pkg...]       extract tracker music (.umx) and render to WAV (default: maps' songs)
  mods                 convert mod plugins' SourceArt (Plugins/Mods/*/SourceArt)
  vmf <name...>        convert a Source (Hammer) map listed in vmf_maps.toml, aligned to its MF map
  import [map...]      run the UE editor importer (UnrealEditor-Cmd -run=MFImport)
  light <map...>       bake static lighting (Lightmass) for generated maps
  all <map...>         map + textures + sounds + import + light

Examples:
  python3 Tools/mfport/mfport.py all mf-warehouse
  python3 Tools/mfport/mfport.py map mf-polar mf-ghetto && python3 Tools/mfport/mfport.py textures

See Docs/Pipeline.md.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mflib import config  # noqa: E402


def converted_maps(cfg) -> list[str]:
    return sorted(p.parent.name for p in (cfg.out_dir / 'Maps').glob('*/level.json'))


def cmd_map(cfg, names):
    from mflib.level import LevelConverter
    for n in names:
        LevelConverter(cfg, n).run()


def cmd_vmf(cfg, names):
    import tomllib
    from mflib.vmf import VmfConverter
    presets = tomllib.loads((Path(__file__).resolve().parent / 'vmf_maps.toml').read_text())
    for n in names:
        p = presets[n]
        src = Path(__file__).resolve().parent / p["vmf"]
        VmfConverter(cfg, src, n, scale=p.get('scale', 1.0), offset=tuple(p.get('offset', (0, 0, 0))),
                     yaw=p.get('yaw', 0.0)).run()


def collect_usage(cfg) -> dict[str, set[str]]:
    usage: dict[str, set[str]] = {}
    for lj in (cfg.out_dir / 'Maps').glob('*/level.json'):
        for k, modes in json.loads(lj.read_text())['texture_usage'].items():
            usage.setdefault(k, set()).update(modes)
    return usage


def cmd_textures(cfg):
    from mflib import textures
    usage = collect_usage(cfg)
    for k, modes in textures.UI_TEXTURES.items():
        usage.setdefault(k, set()).update(modes)
    packages = sorted({k.split('.')[0] for k in usage} - {'engine', 'none'})
    manifest = {}
    for pkg in packages:
        print(f'[textures] {pkg}')
        try:
            manifest.update(textures.convert(cfg, pkg, usage))
        except FileNotFoundError as e:
            print(f'  ! {e}')
    print(f'  -> {textures.write_manifest(cfg, manifest)} ({len(manifest)} textures)')


def cmd_sounds(cfg, packages):
    from mflib import sounds
    if not packages:
        wanted = set(sounds.GAMEPLAY_PACKAGES)
        for lj in (cfg.out_dir / 'Maps').glob('*/level.json'):
            wanted |= sounds.packages_used(json.loads(lj.read_text()))
        packages = sorted(wanted)
    for pkg in packages:
        try:
            sounds.convert_package(cfg, pkg)
        except Exception as e:  # missing / unexportable package
            print(f'  ! {pkg}: {e}')


def cmd_music(cfg, packages):
    from mflib import music
    if not packages:
        wanted = set()
        for lj in (cfg.out_dir / 'Maps').glob('*/level.json'):
            wanted |= music.packages_used(json.loads(lj.read_text()))
        packages = sorted(wanted)
    for pkg in packages:
        try:
            music.convert(cfg, pkg)
        except Exception as e:
            print(f'  ! {pkg}: {e}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('step', choices=['map', 'textures', 'sounds', 'music', 'mods', 'vmf', 'import', 'light', 'all'])
    ap.add_argument('names', nargs='*')
    ap.add_argument('--steps', default='textures,sounds,music,plugins,maps', help='importer steps for `import`')
    a = ap.parse_args()
    cfg = config.load()
    from mflib import ue

    if a.step in ('map', 'all'):
        cmd_map(cfg, a.names or ['mf-warehouse'])
    if a.step == 'vmf':
        cmd_vmf(cfg, a.names or ['mfs-warehouse'])
    if a.step in ('textures', 'all'):
        cmd_textures(cfg)
    if a.step == 'sounds':
        cmd_sounds(cfg, a.names)
    if a.step == 'music':
        cmd_music(cfg, a.names)
    if a.step in ('mods', 'all'):
        from mflib import mods
        mods.convert(cfg)
    if a.step == 'all':
        cmd_sounds(cfg, [])
        cmd_music(cfg, [])
    if a.step in ('import', 'all'):
        rc = ue.run_importer(cfg, a.steps, a.names if a.step == 'import' else a.names or None)
        if rc:
            sys.exit(rc)
    if a.step in ('light', 'all'):
        sys.exit(ue.build_lighting(cfg, a.names or converted_maps(cfg)))


if __name__ == '__main__':
    main()
