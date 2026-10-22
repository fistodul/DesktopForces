#!/usr/bin/env python3
"""Find the Windows installation from Linux (dual boot): the mounted Windows system partition,
the Windows user and the project's place there. The Linux-side tools use it so nobody has to
edit paths for their own machine.

Environment overrides: WIN_ROOT (where C: is mounted), WIN_USER, WIN_DRIVE (letter of that
partition, default C), WIN_PROJECT (the project folder on it).

    python3 Tools/winpaths.py                 everything, KEY=value lines (eval-able)
    python3 Tools/winpaths.py root|user|project|launcher-dat
    python3 Tools/winpaths.py to-windows <linux path on the partition>
"""
from __future__ import annotations

import getpass
import os
import subprocess
import sys
from pathlib import Path

NTFS_TYPES = ('ntfs3', 'ntfs', 'fuseblk', 'ntfs-3g')
# Windows' own accounts (WsiAccount: Windows Sandbox / app isolation; WDAG: Application Guard)
SKIP_USERS = {'public', 'default', 'default user', 'all users', 'defaultapppool', 'wdagutilityaccount', 'wsiaccount'}


def mounts() -> list[Path]:
    """Mounted NTFS partitions."""
    try:
        out = subprocess.run(['findmnt', '-rn', '-o', 'TARGET,FSTYPE'], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        out = Path('/proc/mounts').read_text()
        out = '\n'.join(f'{l.split()[1]} {l.split()[2]}' for l in out.splitlines() if len(l.split()) > 2)
    found = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[1] in NTFS_TYPES:
            found.append(Path(parts[0].replace('\\x20', ' ')))
    return found


def root() -> Path:
    """The Windows system partition (the one with Windows/System32 and Users)."""
    if os.environ.get('WIN_ROOT'):
        return Path(os.environ['WIN_ROOT'])
    candidates = [m for m in mounts() if (m / 'Users').is_dir()]
    system = [m for m in candidates if (m / 'Windows' / 'System32').is_dir()]
    pick = (system or candidates)
    if not pick:
        sys.exit('No mounted Windows partition found (mount it, or set WIN_ROOT)')
    return pick[0]


def user(base: Path | None = None) -> str:
    """The Windows account: WIN_USER, the only real one, the one named like the Linux user, or
    the one that already has an Unreal Projects folder."""
    if os.environ.get('WIN_USER'):
        return os.environ['WIN_USER']
    users_dir = (base or root()) / 'Users'
    names = [p.name for p in users_dir.iterdir() if p.is_dir() and p.name.lower() not in SKIP_USERS and (p / 'Documents').is_dir()]
    if len(names) == 1:
        return names[0]
    me = getpass.getuser().lower()
    for n in names:
        if n.lower() == me:
            return n
    with_ue = [n for n in names if (users_dir / n / 'Documents' / 'Unreal Projects').is_dir()]
    if len(with_ue) == 1:
        return with_ue[0]
    # else whoever logged in last (their registry hive was written last)
    def last_login(n: str) -> float:
        hive = users_dir / n / 'NTUSER.DAT'
        return hive.stat().st_mtime if hive.exists() else 0.0
    if names:
        return max(names, key=last_login)
    sys.exit(f'No Windows user found under {users_dir}: set WIN_USER')


def project() -> Path:
    """C:\\Users\\<you>\\DesktopForces: short (Windows builds trip over 260-character paths deep in
    Intermediate) and writable from Linux (Documents carries Windows' read-only folder flag,
    which ntfs3 honours). An existing Documents\\Unreal Projects\\DesktopForces is kept."""
    if os.environ.get('WIN_PROJECT'):
        return Path(os.environ['WIN_PROJECT'])
    r = root()
    home = r / 'Users' / user(r)
    old = home / 'Documents' / 'Unreal Projects' / 'DesktopForces'
    return old if old.is_dir() else home / 'DesktopForces'


def launcher_dat() -> Path:
    """Epic Games Launcher's list of installed engines."""
    return root() / 'ProgramData' / 'Epic' / 'UnrealEngineLauncher' / 'LauncherInstalled.dat'


def to_windows(path: str | Path) -> str:
    """/media/me/OS/Program Files/x -> C:\\Program Files\\x"""
    r = root()
    rel = Path(path).resolve().relative_to(r.resolve())
    return f'{os.environ.get("WIN_DRIVE", "C")}:\\' + str(rel).replace('/', '\\')


def main(argv: list[str]) -> None:
    if not argv:
        r = root()
        print(f"WIN_ROOT='{r}'\nWIN_USER='{user(r)}'\nWIN_PROJECT='{project()}'")
        return
    what = argv[0]
    if what == 'to-windows':
        print(to_windows(argv[1]))
    else:
        print({'root': root, 'user': user, 'project': project, 'launcher-dat': launcher_dat}[what]())


if __name__ == '__main__':
    main(sys.argv[1:])
