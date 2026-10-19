"""Run Unreal Editor commandlets for the import / lighting steps."""
from __future__ import annotations

import os
import subprocess
import threading
from pathlib import Path

from .config import Config


def editor_cmd(cfg: Config) -> Path:
    if os.name == 'nt':
        return cfg.ue_root / 'Engine' / 'Binaries' / 'Win64' / 'UnrealEditor-Cmd.exe'
    return cfg.ue_root / 'Engine' / 'Binaries' / 'Linux' / 'UnrealEditor-Cmd'


def run_commandlet(cfg: Config, name: str, *args: str, watch: tuple[str, ...] = ()) -> int:
    """Run a commandlet, echoing its output. Lines containing any `watch` text make it count
    as failed even if the editor exits 0 (Lightmass crashes in a child process)."""
    cmd = [str(editor_cmd(cfg)), str(cfg.uproject), f'-run={name}', *args,
           '-unattended', '-nopause', '-nosplash', '-stdout', '-FullStdOutLogOutput']
    print('>', ' '.join(cmd))
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    hits = []

    def pump():
        for line in proc.stdout:
            print(line, end='')
            if any(w in line for w in watch):
                hits.append(line.strip())
    # read on a thread: daemons the editor forks (trace server) inherit the pipe, so EOF may
    # never come - stop reading once the editor itself has exited
    reader = threading.Thread(target=pump, daemon=True)
    reader.start()
    rc = proc.wait()
    reader.join(timeout=5)
    if hits and rc == 0:
        print('\n! failed:', *dict.fromkeys(hits[:5]), sep='\n  ')
        rc = 1
    return rc


def run_importer(cfg: Config, steps: str = 'textures,sounds,music,plugins,maps', maps: list[str] | None = None) -> int:
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
        # our own maps (MF_*) live in /Game/MF/Maps, converted ones in /Game/MFMaps/Generated
        pkg = f'/Game/MF/Maps/{m}' if m.startswith('MF_') else f'/Game/MFMaps/Generated/{m}'
        if not (cfg.uproject.parent / 'Content' / (pkg[len('/Game/'):] + '.umap')).exists():
            print(f'! no map {pkg} - import it first')
            rc = 1
            continue
        # -messaging: commandlets keep UdpMessaging off, and Lightmass talks to the editor
        # through it (without: "Timed out waiting for the recipient", then Lightmass dies)
        rc |= run_commandlet(cfg, 'ResavePackages', '-buildlighting', '-BuildNavigationData', f'-quality={quality}',
                             '-allowcommandletrendering', '-messaging', f'-map={pkg}',
                             watch=('Lightmass crashed', 'Assertion failed', 'Lighting build failed',
                                    'Failed to build lighting'))
    return rc
