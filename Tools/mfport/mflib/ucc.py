"""Run the original game's UCC.exe to export assets (natively on Windows, via Wine elsewhere).

UCC parses its whole command line *including its own path* and splits it at
spaces, so it fails when started from ".../Mobile Forces/System/UCC.exe"
("Failed to find object 'Class Any.Forces\\System\\UCC.exe'"). We therefore start
it through a path without spaces: a symlink under ~/.cache on Linux/macOS, the 8.3
short path on Windows. Output paths may contain spaces (they are quoted).

Verified exports (package version 83):
  Texture -> .bmp (8-bit paletted, palette index 0 = mask colour)
  Sound   -> .wav
  Music   -> tracker module (.it/.s3m/.xm/.umx payload)
  StaticMesh -> .t3d (triangles with normals, UVs and texture paths)
  Level   -> .t3d (all actors, CSG brushes, embedded static meshes)
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

from .config import Config


def _spaceless_game_dir(cfg: Config) -> Path:
    if os.name == 'nt':
        import ctypes
        buf = ctypes.create_unicode_buffer(1024)
        ctypes.windll.kernel32.GetShortPathNameW(str(cfg.game_dir), buf, 1024)
        if not buf.value or ' ' in buf.value:
            # the drive keeps no 8.3 names (often off on non-system drives)
            raise RuntimeError(f'{cfg.game_dir} has spaces and no short name: point game_dir at a path '
                               'without spaces, e.g. a junction: mklink /J C:\\MobileForces "<your install>"')
        return Path(buf.value)
    link = Path.home() / '.cache' / 'mfport' / 'game'
    if ' ' in str(link):
        raise RuntimeError(f'{link} contains a space; set XDG/HOME to a path without spaces')
    link.parent.mkdir(parents=True, exist_ok=True)
    target = cfg.game_dir.resolve()
    if link.is_symlink() and Path(os.readlink(link)) != target:
        link.unlink()
    if not link.exists():
        link.symlink_to(target)
    return link


def _win_path(p: Path) -> str:
    if os.name == 'nt':
        return str(p)
    return 'Z:' + str(p.resolve()).replace('/', '\\')


def run(cfg: Config, *args: str, log=None) -> str:
    """Run UCC.exe with the given arguments from the game's System dir; return stdout."""
    game = _spaceless_game_dir(cfg)
    system = game / 'System'
    cmd = ([str(system / 'UCC.exe')] if os.name == 'nt' else [cfg.wine, './UCC.exe']) + list(args)
    # Wine takes the working directory from $PWD; keep it the spaceless symlink path
    env = dict(os.environ, WINEDEBUG='-all', PWD=str(system))
    # a wedged Wine (UCC.exe left defunct) never returns: give up after 10 minutes
    proc = subprocess.run(cmd, cwd=system, env=env, capture_output=True, text=True, errors='replace', timeout=600)
    out = proc.stdout
    if log:
        log.write(out)
    failed = 'Exiting due to error' in out or proc.returncode not in (0, 1)
    # UCC can crash while shutting down after the command itself finished (mf-nc:
    # "Unhashed name ..." after "Success - 0 error(s)"): the command's own verdict stands
    done = out.rfind('Success - 0 error(s)')
    if failed and done >= 0 and 'Exiting due to error' in out[done:]:
        failed = False
    if failed:
        raise RuntimeError(f'UCC failed ({proc.returncode}): {" ".join(args)}\n{out[-2000:]}')
    return out


def batch_export(cfg: Config, package_rel: str, cls: str, ext: str, out_dir: Path) -> list[str]:
    """`ucc batchexport <package> <class> <ext> <dir>`; package_rel like 'Maps/mf-warehouse.umf'."""
    out_dir.mkdir(parents=True, exist_ok=True)
    pkg = '..\\' + package_rel.replace('/', '\\')
    out = run(cfg, 'batchexport', pkg, cls, ext, _win_path(out_dir))
    return [line for line in out.splitlines() if line.startswith('Exported ')]


if __name__ == '__main__':
    from .config import load
    print(run(load(), *sys.argv[1:]))
