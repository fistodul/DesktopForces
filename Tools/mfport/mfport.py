#!/usr/bin/env python3
"""mfport - Mobile Forces asset pipeline (your original game install -> UE5 project).

Steps (run alone or chained with `all`):
  map <name...>        convert maps: BSP world geometry, static meshes, actors (UCC T3D export)
  props                gameplay meshes from the game packages (vehicles, crates, safes, flag bases ...)
  textures             export + convert every texture the converted maps use
  sounds [pkg...]      export + convert sounds (default: gameplay packages + map ambience)
  music [pkg...]       extract tracker music (.umx) and render to WAV (default: maps' songs)
  effects              sprite particle effects: flipbook textures + emitter data (built-in + maps')
  skeletal             soldiers and first-person weapons: skeletal meshes + animations as glTF
  vmf <name...>        convert a Source (Hammer) map listed in vmf_maps.toml, aligned to its MF map
  merge <preset...>    merge two aligned maps area by area (merge/<preset>.toml)
  packs                which pack each converted map is in (core / classic), and what core maps
                       still need from the original game (packs.toml)
  import [map...]      run the UE editor importer (UnrealEditor-Cmd -run=DFImport)
  light <map...>       bake static lighting (Lightmass) for generated maps
  all <map...>         map + textures + effects + sounds + import + light
  remaining            resume missing playable maps; --limit N bounds the batch

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
                     yaw=p.get('yaw', 0.0), zscale=p.get('zscale'), prop_scale=p.get('prop_scale')).run()


def collect_usage(cfg) -> dict[str, set[str]]:
    usage: dict[str, set[str]] = {}
    files = list((cfg.out_dir / 'Maps').glob('*/level.json')) + list((cfg.out_dir / 'Props').glob('props.json')) \
        + list((cfg.out_dir / 'Skeletal').glob('skeletal.json'))
    for lj in files:
        for k, modes in json.loads(lj.read_text())['texture_usage'].items():
            usage.setdefault(k, set()).update(modes)
    return usage


def cmd_textures(cfg):
    from mflib import textures
    usage = collect_usage(cfg)
    for k, modes in textures.UI_TEXTURES.items():
        usage.setdefault(k, set()).update(modes)
    from mflib import fonts
    for k, modes in fonts.texture_usage(cfg).items():   # MF bitmap fonts' pages
        usage.setdefault(k, set()).update(modes)
    fonts.write(cfg)
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
    ap.add_argument('step', choices=['map', 'props', 'textures', 'effects', 'skeletal', 'soldiers', 'sounds', 'music', 'vmf', 'merge', 'packs', 'import', 'light', 'all', 'remaining'])
    ap.add_argument('names', nargs='*')
    ap.add_argument('--steps', default='art,textures,props,effects,skeletal,sounds,music,maps', help='importer steps for `import`')
    ap.add_argument('--limit', type=int, default=0, help='maximum maps for a remaining batch (0: all)')
    a = ap.parse_args()
    cfg = config.load()
    from mflib import ue

    if a.step == 'remaining':
        from mflib.remaining import run
        sys.exit(run(cfg, a.limit))

    if a.step in ('map', 'all'):
        cmd_map(cfg, a.names or ['mf-warehouse'])
    if a.step == 'vmf':
        cmd_vmf(cfg, a.names or ['mfs-warehouse'])
    if a.step == 'merge':
        from mflib.merge import Merger
        for n in a.names or ['mf-warehouse']:
            Merger(cfg, n).run()
    if a.step in ('props', 'all'):
        from mflib import props
        print(f'  -> {props.convert(cfg)}')
    if a.step in ('textures', 'all'):
        cmd_textures(cfg)
    if a.step in ('effects', 'all'):
        from mflib import effects
        print(f'  -> {effects.convert(cfg)}')
    if a.step in ('skeletal', 'soldiers', 'all'):
        from mflib import skelmesh
        print(f'  -> {skelmesh.convert(cfg)}')
    if a.step == 'sounds':
        cmd_sounds(cfg, a.names)
    if a.step == 'music':
        cmd_music(cfg, a.names)
    if a.step == 'all':
        cmd_sounds(cfg, [])
        cmd_music(cfg, [])
    if a.step == 'packs':
        from mflib import packs
        packs.report(cfg)
    if a.step in ('import', 'all'):
        from mflib import packs
        packs.write_manifest(cfg)
        if 'art' in a.steps.split(','):
            # the game's own art (Tools/artgen: generated + SourceArt) goes in with the same run
            sys.path.insert(0, str(config.PROJECT_DIR / 'Tools' / 'artgen'))
            import artgen
            artgen.main([])
        rc = ue.run_importer(cfg, a.steps, a.names if a.step == 'import' else a.names or None)
        if rc:
            sys.exit(rc)
    if a.step in ('light', 'all'):
        sys.exit(ue.build_lighting(cfg, a.names or converted_maps(cfg)))


if __name__ == '__main__':
    main()
