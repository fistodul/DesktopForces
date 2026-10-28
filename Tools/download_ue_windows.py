#!/usr/bin/env python3
"""Download the project's Windows engine using Legendary from PATH.

Install Legendary in your preferred environment and run `legendary auth` once.
    python3 Tools/download_ue_windows.py --target-dir '/mounted/drive/Epic Games/UE_5.8'
    python3 Tools/download_ue_windows.py --target-dir 'D:/Epic Games/UE_5.8' --dry-run

No LauncherInstalled.dat edits: use UE_ROOT for a nonstandard install location.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys


def commands(target: Path, app: str, symbols: bool, workers: int) -> list[list[str]]:
    install = ['legendary', 'install', app, '--platform', 'Windows',
               '--base-path', str(target.parent), '--game-folder', target.name,
               '--install-tag=', '--skip-dlcs', '--max-workers', str(workers)]
    if symbols:
        install.append('--install-tag=editor_symbols')
    # Populate UE metadata before install (Legendary's regular game list excludes UE).
    return [['legendary', 'list-games', '--include-ue', '--platform', 'Windows'], install]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--target-dir', type=Path, required=True)
    parser.add_argument('--include-symbols', action='store_true')
    parser.add_argument('--max-workers', type=int, default=8)
    parser.add_argument('--dry-run', action='store_true', help='Print commands; no login, network or download')
    args = parser.parse_args()
    if args.max_workers < 1:
        parser.error('--max-workers must be positive')
    project = Path(__file__).resolve().parents[1] / 'DesktopForces.uproject'
    app = 'UE_' + json.loads(project.read_text(encoding='utf-8'))['EngineAssociation']
    for cmd in commands(args.target_dir.expanduser().resolve(), app, args.include_symbols, args.max_workers):
        print(json.dumps(cmd), flush=True)
        if not args.dry_run:
            if not shutil.which('legendary'):
                parser.error('legendary is not on PATH. Install legendary-gl in your environment, then run legendary auth.')
            subprocess.run(cmd, check=True)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        sys.exit(str(exc))
