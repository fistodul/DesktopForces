#!/usr/bin/env python3
"""Desktop Forces — first-time setup and asset import helper.

Run this after cloning the repo to set up the project and import game content.
Without arguments it walks you through everything interactively.

Quick usage:
  python3 Tools/setup.py                  # interactive setup
  python3 Tools/setup.py --check          # just check prerequisites
  python3 Tools/setup.py --import-all     # import all core maps non-interactively
  python3 Tools/setup.py --list-maps      # show available maps
"""
import json
import platform
import shutil
import subprocess
import sys
import textwrap
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
MFPORT_DIR = SCRIPT_DIR / 'mfport'
TOML_EXAMPLE = MFPORT_DIR / 'mfport.example.toml'
TOML_USER = MFPORT_DIR / 'mfport.toml'

# The core maps that have been cleared by their authors (from packs.toml).
CORE_MAPS = {
    'BR-thebigisland':      "Battle Royale island (Filip + Gregaras mashup)",
    'COOP-CoopOneUpdate':   "Co-op survival map (AKVARI)",
    'dm-HolidayTown':       "Deathmatch - Holiday Town (Marcin)",
    'DM-Warship':           "Deathmatch - Warship (Marcinbydg)",
    'MF-Aquarius':          "Team game - Aquarius (Gian)",
    'mf-BraveDwarfes':      "Team game - Brave Dwarves (Marcin)",
    'mf-City_Rush':         "Team game - City Rush (Marcinbydg)",
    'MF-Filip':             "Team game - Filip (AKVARI)",
    'mf-Pobojowisko_VerII': "Team game - Pobojowisko Ver.II (Marcin)",
    'mf-Prehistoric':       "Team game - Prehistoric (Marcinbydg)",
    'MF-RandomLand':        "Team game - Random Land (AKVARI)",
    'mf-Sardine_City':      "Team game - Sardine City (Marcinbydg)",
    'mf-Space_station':     "Team game - Space Station (Marcin)",
    'MF-Stuck':             "Team game - Stuck (AKVARI)",
    'mf-Tajga':             "Team game - Tajga (Marcin)",
    'MF-Test_Facility':     "Team game - Test Facility (Ahmed Isaac Alizai)",
    'mf-Toreford':          "Team game - Toreford (Marcin)",
    'mf-Treasure_Finders':  "Team game - Treasure Finders (Marcin)",
    'mf-Tropics':           "Team game - Tropics (Marcinbydg)",
    'MF-TurpowiaV1':        "Team game - Turpowia V1 (AKVARI)",
    'mf-Worlds_Peak':       "Team game - World's Peak (Marcin)",
    'ZM-SpaceZombura':      "Zombie survival - Space Zombura (AKVARI)",
}


def banner(text):
    w = max(len(text) + 4, 50)
    print()
    print('=' * w)
    print(f'  {text}')
    print('=' * w)


def ask(prompt, choices=None, default=None):
    """Ask the user a question. If choices is a list, show numbered options."""
    if choices:
        print()
        for i, c in enumerate(choices, 1):
            marker = ' *' if c == default else ''
            print(f'  {i}) {c}{marker}')
        while True:
            raw = input(f'\n{prompt} [1-{len(choices)}]: ').strip()
            if not raw and default:
                return default
            try:
                idx = int(raw)
                if 1 <= idx <= len(choices):
                    return choices[idx - 1]
            except ValueError:
                if raw in choices:
                    return raw
            print(f'  Please enter a number from 1 to {len(choices)}.')
    else:
        raw = input(f'{prompt} [{default}]: ').strip() if default else input(f'{prompt}: ').strip()
        return raw or default


def ask_yn(prompt, default=True):
    suffix = ' [Y/n]' if default else ' [y/N]'
    raw = input(prompt + suffix + ': ').strip().lower()
    if not raw:
        return default
    return raw.startswith('y')


def check_python():
    v = sys.version_info
    ok = v >= (3, 11)
    print(f'  Python {v.major}.{v.minor}.{v.micro}' + (' ✓' if ok else ' ✗ (need 3.11+)'))
    return ok


def check_wine():
    if platform.system() == 'Windows':
        print('  Wine: not needed (Windows) ✓')
        return True
    wine = shutil.which('wine')
    if wine:
        try:
            ver = subprocess.check_output([wine, '--version'], text=True, stderr=subprocess.DEVNULL).strip()
            print(f'  Wine: {ver} ✓')
            return True
        except Exception:
            pass
    print('  Wine: not found ✗  (needed on Linux/macOS to run UCC.exe)')
    print('        Install: sudo apt install wine  or  brew install wine-stable')
    return False


def check_ue():
    """Try to find an Unreal Engine installation."""
    from mfport.mflib.config import load
    try:
        root = load().ue_root
        if (root / 'Engine').is_dir():
            print(f'  Unreal Engine: {root} ✓')
            return str(root)
    except (OSError, ValueError) as exc:
        print(f'  Unreal Engine: {exc}')
    return None


def find_game_install():
    """Try to find a Mobile Forces installation."""
    from mfport.mflib.config import load
    try:
        configured = load().game_dir
        if (configured / 'System').is_dir() and (configured / 'Maps').is_dir():
            print(f'  Mobile Forces: {configured} ✓')
            return str(configured)
    except (OSError, ValueError):
        pass
    candidates = []
    if platform.system() == 'Linux':
        # Steam common locations
        steam_dirs = [
            Path.home() / '.steam/steam/steamapps/common/Mobile Forces',
            Path.home() / '.local/share/Steam/steamapps/common/Mobile Forces',
        ]
        candidates += steam_dirs
        # Nearby directories
        candidates += list(PROJECT_DIR.parent.glob('MoFo/MobileForces'))
        candidates += list(PROJECT_DIR.parent.glob('MobileForces'))
        candidates += list(PROJECT_DIR.parent.parent.glob('MoFo/MobileForces'))
    elif platform.system() == 'Windows':
        candidates += [
            Path('C:/Program Files (x86)/Steam/steamapps/common/Mobile Forces'),
            Path('D:/SteamLibrary/steamapps/common/Mobile Forces'),
        ]
        candidates += list(PROJECT_DIR.parent.glob('MoFo/MobileForces'))

    for c in candidates:
        if (c / 'System').is_dir() and (c / 'Maps').is_dir():
            print(f'  Mobile Forces: {c} ✓')
            return str(c)

    print('  Mobile Forces: not auto-detected')
    print('        You need a copy of the original game (Steam, GOG, or retail CD)')
    return None


def create_config(game_dir, ue_root):
    """Create mfport.toml from the example, filling in detected paths."""
    template = TOML_EXAMPLE.read_text(encoding='utf-8')
    # JSON string quoting is TOML-compatible for paths, including backslashes/quotes.
    # Platform sections keep a Windows setup from invalidating the Linux configuration.
    section = ''
    paths = {'game_dir': game_dir, 'ue_root': ue_root}
    lines = []
    for line in template.splitlines():
        if line.startswith('['):
            section = line.strip('[]')
        key = line.split('=', 1)[0].strip()
        if section == platform.system().lower() and key in paths and paths[key]:
            line = f'{key} = {json.dumps(str(paths[key]), ensure_ascii=False)}'
        lines.append(line)
    TOML_USER.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'\n  Created {TOML_USER}')


def list_maps():
    banner('Available Maps (Core Pack — cleared by authors)')
    print()
    max_name = max(len(n) for n in CORE_MAPS)
    for name, desc in sorted(CORE_MAPS.items()):
        print(f'  {name:<{max_name}}  {desc}')
    print(f'\n  Total: {len(CORE_MAPS)} maps')


def select_maps():
    """Let the user pick maps to import."""
    sorted_maps = sorted(CORE_MAPS.keys())
    print()
    for i, name in enumerate(sorted_maps, 1):
        desc = CORE_MAPS[name]
        print(f'  {i:>2}) {name:<28} {desc}')

    print(f'\n   a) All {len(sorted_maps)} maps')
    print(f'   q) Cancel')

    raw = input('\nEnter map numbers (comma-separated), "a" for all, or "q" to cancel: ').strip().lower()
    if raw == 'q':
        return []
    if raw == 'a':
        return sorted_maps

    selected = []
    for part in raw.split(','):
        part = part.strip()
        try:
            idx = int(part)
            if 1 <= idx <= len(sorted_maps):
                selected.append(sorted_maps[idx - 1])
        except ValueError:
            # Try matching by name
            for m in sorted_maps:
                if m.lower().startswith(part.lower()):
                    selected.append(m)
                    break
    return selected


def run_mfport(step, names=None):
    """Run mfport.py with the given step and map names."""
    cmd = [sys.executable, str(MFPORT_DIR / 'mfport.py'), step]
    if names:
        cmd.extend(names)
    print(f'\n  Running: {" ".join(cmd)}\n')
    return subprocess.call(cmd, cwd=str(PROJECT_DIR))


def main():
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true', help='Check prerequisites only')
    ap.add_argument('--list-maps', action='store_true', help='List available core maps')
    ap.add_argument('--import-all', action='store_true', help='Import all core maps non-interactively')
    args = ap.parse_args()

    if args.list_maps:
        list_maps()
        return

    banner('Desktop Forces — Project Setup')

    # ── Prerequisites ──
    print('\nChecking prerequisites...\n')
    py_ok = check_python()
    wine_ok = check_wine()
    ue_root = check_ue()
    game_dir = find_game_install()

    has_config = TOML_USER.exists()
    print(f'  mfport.toml: {"exists ✓" if has_config else "not yet created"}')

    if args.check:
        print()
        if py_ok and wine_ok:
            print('Prerequisites OK. Run without --check to continue setup.')
        else:
            print('Some prerequisites are missing. See above.')
        return

    if args.import_all:
        if not has_config:
            if game_dir or ue_root:
                create_config(game_dir, ue_root)
            else:
                print('\nError: No mfport.toml and could not auto-detect paths.')
                print('Run interactively first: python3 Tools/setup.py')
                sys.exit(1)
        print('\nImporting all core maps...')
        maps = sorted(CORE_MAPS.keys())
        rc = run_mfport('all', maps)
        sys.exit(rc)

    # ── Interactive setup ──
    if not has_config:
        banner('Configuration')
        if not game_dir:
            game_dir = ask('Path to your Mobile Forces install (the folder with System/, Maps/)')
            if game_dir and not (Path(game_dir) / 'System').is_dir():
                print(f'  Warning: {game_dir}/System not found — double-check the path')
        if not ue_root:
            ue_root = ask('Path to Unreal Engine root (the folder with Engine/)', default='')

        create_config(game_dir, ue_root)
    else:
        print(f'\n  Using existing config: {TOML_USER}')

    # ── What to do ──
    banner('Import Content')
    print(textwrap.dedent("""
        The source code compiles into a playable game, but without imported content
        you'll only see the built-in test range (MF_TestRange — the grey map with a
        ball-throwing mechanism).

        To get actual maps, weapons, vehicles and textures you need to run the asset
        pipeline, which reads your Mobile Forces installation and converts everything
        into UE5 assets.
    """))

    action = ask('What would you like to do?', choices=[
        'Import all core maps (recommended for first setup)',
        'Pick specific maps to import',
        'Import only props/textures/sounds (no maps)',
        'Just check the config and exit',
    ])

    if action.startswith('Import all'):
        maps = sorted(CORE_MAPS.keys())
        print(f'\n  Will import {len(maps)} maps with all assets.')
        if ask_yn('  Continue?'):
            run_mfport('all', maps)

    elif action.startswith('Pick specific'):
        maps = select_maps()
        if maps:
            print(f'\n  Selected: {", ".join(maps)}')
            if ask_yn('  Continue?'):
                run_mfport('all', maps)
        else:
            print('  No maps selected.')

    elif action.startswith('Import only'):
        steps = ask('Which steps? (comma-separated)', default='props,textures,sounds,skeletal')
        for step in steps.split(','):
            step = step.strip()
            if step:
                run_mfport(step)

    else:
        print('\n  Config looks good. Run this script again when ready to import.')

    print('\nDone. Open the project in Unreal Editor to see imported content.')
    print('If maps are dark, bake lighting: python3 Tools/mfport/mfport.py light <map-name>')


if __name__ == '__main__':
    main()
