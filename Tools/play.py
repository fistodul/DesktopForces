#!/usr/bin/env python3
"""Run/build Desktop Forces on Linux or Windows (Python 3.11+).

Run from any directory; on Windows, use `py -3` instead of `python3` if needed:

    python3 Tools/play.py                         # warehouse, deathmatch, no bots
    python3 Tools/play.py mf-warehouse tdm 8       # team deathmatch with 8 bots
    python3 Tools/play.py mf-warehouse koh 8       # King of the Hill
    python3 Tools/play.py MF_TestRange            # built-in test range
    python3 Tools/play.py editor                  # Unreal Editor
    python3 Tools/play.py build                   # compile editor/game modules
    python3 Tools/play.py build -MaxParallelActions=4
    python3 Tools/play.py mf-warehouse dm 0 -windowed -ResX=1600 -ResY=900
    python3 Tools/play.py /Engine/Maps/Entry /Script/DesktopForces.DFMenuGameMode 0
    python3 Tools/play.py --dry-run mf-warehouse dm 0 -RenderOffscreen -nosound
    python3 Tools/play.py --engine-root           # engine path used by other tools

Modes: dm, tdm, holdout, sc, ctf, det, cap, trailer, koh, tug, coop, zombie,
       arena, deball, br; a full game-mode class path also works.
Extra arguments go to Unreal. DF_OPTS appends URL options, e.g. ?TimeLimit=5.
UE_ROOT > MFPORT_UE_ROOT > mfport.toml platform section > shared keys > discovery.
"""
from __future__ import annotations

import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

from mfport.mflib.config import Config, load


def windows_build_prefix(root: Path) -> list[str]:
    """Installed-engine UBT, with Epic's bundled runtime and no cmd.exe interpolation."""
    ubt = root / 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
    if not ubt.is_file():
        raise FileNotFoundError(f'{ubt} missing; repair the engine install (Epic Games Launcher: Verify).')
    if os.environ.get('UE_USE_SYSTEM_DOTNET') == '1':
        dotnet = shutil.which('dotnet')
    else:
        arm = os.environ.get('PROCESSOR_ARCHITECTURE') == 'ARM64' or os.environ.get('ProgramFiles(Arm)')
        arch = 'win-arm64' if arm else 'win-x64'
        runtimes = list((root / 'Engine/Binaries/ThirdParty/DotNet').glob(f'*/{arch}/dotnet.exe'))

        def version(runtime):
            return tuple(int(part) for part in re.findall(r'\d+', runtime.parents[1].name))

        dotnet = str(max(runtimes, key=version)) if runtimes else None
    if not dotnet:
        raise FileNotFoundError(f'No Windows .NET runtime found in {root}; repair the engine installation or set UE_USE_SYSTEM_DOTNET=1.')
    return [dotnet, str(ubt)]


def command(cfg: Config, args: list[str], system: str) -> list[str]:
    if system not in ('Linux', 'Windows'):
        raise ValueError(f'Unsupported host: {system}; use Linux or Windows.')
    windows = system == 'Windows'
    host = 'Win64' if windows else 'Linux'
    editor = cfg.ue_root / 'Engine/Binaries' / host / ('UnrealEditor.exe' if windows else 'UnrealEditor')
    project = str(cfg.uproject)
    if args and args[0] == 'build':
        batch = cfg.ue_root / 'Engine/Build/BatchFiles'
        prefix = windows_build_prefix(cfg.ue_root) if windows else [str(batch / 'Linux/Build.sh')]
        return [*prefix, 'DesktopForcesEditor', host, 'Development', f'-project={project}', '-waitmutex', '-NoUBA', *args[1:]]
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
        root = cfg.uproject.parent
        folders = [(root / 'Content/MF/Maps', '/Game/MF/Maps'), (root / 'Content/Maps', '/Game/Maps')]
        folders += [(d, f'/{d.parent.parent.name}/Maps') for d in sorted(root.glob('Plugins/*/Content/Maps'))]
        found = [mount for folder, mount in folders if (folder / (name + '.umap')).is_file()]
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
        env = os.environ.copy()
        cwd = cfg.uproject.parent
        if os.name == 'nt' and 'DesktopForcesEditor' in args:
            cwd = cfg.ue_root / 'Engine/Source'
            if env.get('UE_USE_SYSTEM_DOTNET') != '1':
                env['DOTNET_ROOT'] = str(Path(args[0]).parent)
                env['DOTNET_MULTILEVEL_LOOKUP'] = '0'
                env['DOTNET_ROLL_FORWARD'] = 'LatestMajor'
                env['PATH'] = env['DOTNET_ROOT'] + os.pathsep + env.get('PATH', '')
        print('Launching:', json.dumps(args), flush=True)
        return subprocess.call(args, cwd=cwd, env=env)
    except (OSError, ValueError) as exc:
        print(f'play: {exc}', file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 130


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
