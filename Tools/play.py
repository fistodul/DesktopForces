#!/usr/bin/env python3
"""Run/build Desktop Forces on Linux or Windows (Python 3.11+).

Run from any directory; on Windows, use `python` (or `py -3`) instead of `python3` if needed:

    python3 play.py                         # warehouse, deathmatch, no bots
    python3 play.py mf-warehouse tdm 8       # team deathmatch with 8 bots
    python3 play.py mf-warehouse koh 8       # King of the Hill
    python3 play.py MF_TestRange            # built-in test range
    python3 play.py editor                  # Unreal Editor
    python3 play.py build                   # compile editor/game modules
    python3 play.py build -MaxParallelActions=4
    python3 play.py mf-warehouse dm 0 -windowed -ResX=1600 -ResY=900
    python3 play.py /Engine/Maps/Entry /Script/DesktopForces.DFMenuGameMode 0
    python3 play.py --dry-run mf-warehouse dm 0 -RenderOffscreen -nosound
    python3 play.py --engine-root           # engine path used by other tools

Modes: dm, tdm, holdout, sc, ctf, det, cap, trailer, koh, tug, coop, zombie,
       arena, moball, br, kf, sw, mr; a full game-mode class path also works.
Extra arguments go to Unreal. DF_OPTS appends URL options, e.g. ?TimeLimit=5.
UE_ROOT > MFPORT_UE_ROOT > mfport.toml platform section > shared keys > discovery.
"""
from __future__ import annotations

import json
import os
import platform
import subprocess
import sys
from pathlib import Path

from mfport.mflib.config import Config, load, map_folders
from mfport.mflib.ue import ubt_command, ubt_launch


def command(cfg: Config, args: list[str], system: str) -> list[str]:
    if system not in ('Linux', 'Windows'):
        raise ValueError(f'Unsupported host: {system}; use Linux or Windows.')
    windows = system == 'Windows'
    host = 'Win64' if windows else 'Linux'
    editor = cfg.ue_root / 'Engine/Binaries' / host / ('UnrealEditor.exe' if windows else 'UnrealEditor')
    project = str(cfg.uproject)
    if args and args[0] == 'build':  # the editor's modules; the game's are Tools/package.py's
        return ubt_command(cfg.ue_root, windows, 'DesktopForcesEditor', host, 'Development', cfg.uproject, *args[1:])
    if args and args[0] == 'editor':
        return [str(editor), project, *args[1:]]
    # Engine flags can follow any number of positional arguments.
    positional = []
    extra = list(args)
    while extra and not extra[0].startswith('-') and len(positional) < 3:
        positional.append(extra.pop(0))
    name = positional[0] if positional else 'mf-warehouse'
    mode = positional[1] if len(positional) > 1 else 'dm'
    bots = positional[2] if len(positional) > 2 else '0'
    if name.startswith('/'):
        map_path = name
    else:
        # wherever the map file is: the game's folders, then any content plugin (a pack)
        found = [mount for folder, mount in map_folders(cfg.uproject.parent) if (folder / (name + '.umap')).is_file()]
        fallback = '/Game/MF/Maps' if name.startswith('MF_') else '/MobileForcesClassic/Maps' # not converted here
        map_path = (found[0] if found else fallback) + '/' + name
    url = f'{map_path}?game={mode}?Bots={bots}' + os.environ.get('DF_OPTS', '')
    return [str(editor), project, url, '-game', *extra]


def main(argv: list[str]) -> int:
    if argv in (["-h"], ["--help"]):
        print(__doc__)
        return 0
    try:
        cfg = load()
        if argv == ['--engine-root']:
            print(cfg.ue_root)
            return 0
        dry = bool(argv and argv[0] == '--dry-run')
        args = command(cfg, argv[1:] if dry else argv, platform.system())
        if dry:
            print(json.dumps(args, indent=2))
            return 0
        if not Path(args[0]).is_file():
            raise FileNotFoundError(f'{args[0]} does not exist. Check UE_ROOT or the current platform section in Tools/mfport/mfport.toml.')
        cwd, env = ubt_launch(cfg.ue_root, args, cfg.uproject.parent)
        print('Launching:', json.dumps(args), flush=True)
        return subprocess.call(args, cwd=cwd, env=env)
    except (OSError, ValueError) as exc:
        print(f'play: {exc}', file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 130


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
