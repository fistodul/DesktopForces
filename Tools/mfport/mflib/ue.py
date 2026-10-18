"""Run Unreal Editor commandlets for the import / lighting steps."""
from __future__ import annotations

import os
import subprocess
from pathlib import Path

from .config import Config


def editor_cmd(cfg: Config) -> Path:
    if os.name == 'nt':
        return cfg.ue_root / 'Engine' / 'Binaries' / 'Win64' / 'UnrealEditor-Cmd.exe'
    return cfg.ue_root / 'Engine' / 'Binaries' / 'Linux' / 'UnrealEditor-Cmd'


def run_commandlet(cfg: Config, name: str, *args: str) -> int:
    cmd = [str(editor_cmd(cfg)), str(cfg.uproject), f'-run={name}', *args,
           '-unattended', '-nopause', '-nosplash', '-stdout', '-FullStdOutLogOutput']
    print('>', ' '.join(cmd))
    return subprocess.run(cmd).returncode


def run_importer(cfg: Config, steps: str = 'textures,sounds,maps', maps: list[str] | None = None) -> int:
    args = [f'-steps={steps}']
    default_out = (cfg.uproject.parent / 'Tools' / 'mfport' / 'work' / 'out').resolve()
    if cfg.out_dir.resolve() != default_out:
        # UE re-joins argv into one command line; quote paths (they may contain spaces)
        args.append(f'-work="{cfg.out_dir}"')
    if maps:
        args.append('-maps=' + ','.join(maps))
    return run_commandlet(cfg, 'MFImport', *args)


def build_lighting(cfg: Config, maps: list[str], quality: str = 'Preview') -> int:
    """Bake static lighting (Lightmass) + precomputed visibility for generated maps."""
    rc = 0
    for m in maps:
        rc |= run_commandlet(cfg, 'ResavePackages', '-buildlighting', f'-quality={quality}',
                             '-allowcommandletrendering', f'-map=/Game/MFMaps/Generated/{m}')
    return rc
