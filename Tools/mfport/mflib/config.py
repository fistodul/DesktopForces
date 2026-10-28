"""Pipeline configuration.

Values come from `Tools/mfport/mfport.toml` (copy `mfport.example.toml`), and any
key can be overridden with an environment variable `MFPORT_<KEY>` (upper case).
"""
from __future__ import annotations

import os
import json
import platform
import tomllib
from dataclasses import dataclass
from pathlib import Path, PureWindowsPath

TOOL_DIR = Path(__file__).resolve().parent.parent          # Tools/mfport
PROJECT_DIR = TOOL_DIR.parent.parent                        # UE project root

# One Mobile Forces / UE1 unit in UE5 centimetres.
# MF's player capsule (radius 17, half-height 39) * 2.25 = 38 x 88 = the UE5
# default character capsule, so stock UE5 animation/camera assumptions fit.
# Every distance, speed and acceleration converted from MF data is multiplied by
# this; gravity too (-950 uu/s^2 -> -2137.5 cm/s^2).
UU_TO_CM = 2.25


@dataclass
class Config:
    game_dir: Path
    work_dir: Path
    ue_root: Path
    uproject: Path
    wine: str
    max_texture_size: int

    @property
    def raw_dir(self) -> Path:
        return self.work_dir / 'raw'

    @property
    def out_dir(self) -> Path:
        return self.work_dir / 'out'


def engine_version(root: Path) -> tuple[int, int, int]:
    try:
        version = json.loads((root / 'Engine/Build/Build.version').read_text(encoding='utf-8'))
        return tuple(int(version[k]) for k in ('MajorVersion', 'MinorVersion', 'PatchVersion'))
    except (OSError, ValueError, KeyError, TypeError):
        return (0, 0, 0)


def discover_engine(uproject: Path) -> Path | None:
    """Newest installed patch matching the project's major/minor, never another UE minor."""
    association = str(json.loads(uproject.read_text(encoding='utf-8')).get('EngineAssociation', '5.8'))
    parts = association.split('.')
    if len(parts) < 2 or not all(v.isdigit() for v in parts[:2]):
        return None  # source-build GUIDs require an explicit ue_root
    wanted = tuple(map(int, parts[:2]))
    candidates = []
    if platform.system() == 'Windows':
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, rf'SOFTWARE\EpicGames\Unreal Engine\{association}') as key:
                candidates.append(Path(winreg.QueryValueEx(key, 'InstalledDirectory')[0]))
        except OSError:
            pass
        launcher = Path(os.environ.get('PROGRAMDATA', 'C:/ProgramData')) / 'Epic/UnrealEngineLauncher/LauncherInstalled.dat'
        try:
            installs = json.loads(launcher.read_text(encoding='utf-8')).get('InstallationList', [])
            candidates += [Path(i['InstallLocation']) for i in installs if 'InstallLocation' in i]
        except (OSError, ValueError):
            pass
        for drive in 'CDEFGHIJKLMNOPQRSTUVWXYZ':
            candidates += list(Path(f'{drive}:/Program Files/Epic Games').glob('UE_*'))
        binary = 'Engine/Binaries/Win64/UnrealEditor.exe'
    else:
        for base in (Path.home(), uproject.parent.parent, Path('/opt')):
            for pattern in ('Linux_Unreal_Engine_*', 'UnrealEngine*', 'UE_*'):
                candidates += list(base.glob(pattern))
        binary = 'Engine/Binaries/Linux/UnrealEditor'
    valid = [c for c in candidates if (c / binary).is_file() and engine_version(c)[:2] == wanted]
    return max(valid, key=engine_version).resolve() if valid else None


def native_path(value: str, base: Path, system: str) -> Path:
    if system != 'Windows' and PureWindowsPath(value).is_absolute():
        raise ValueError(f'Windows path {value!r} cannot be used on {system}. Set it in [windows] and add a [linux] override in mfport.toml.')
    if system == 'Windows' and value.startswith('/'):
        raise ValueError(f'Linux path {value!r} cannot be used on Windows. Add a [windows] override in mfport.toml.')
    path = Path(value).expanduser()
    return path if path.is_absolute() else (base / path).resolve()


def load() -> Config:
    data: dict = {}
    for name in ('mfport.toml', 'mfport.example.toml'):
        f = TOOL_DIR / name
        if f.exists():
            try:
                data = tomllib.loads(f.read_text(encoding='utf-8'))
            except tomllib.TOMLDecodeError as exc:
                raise ValueError(f"Invalid TOML in {f}: {exc}. Use single-quoted Windows paths or forward slashes (C:/Games), not raw backslashes in double quotes.") from exc
            break
    system = platform.system()
    data.update(data.get(system.lower(), {}))

    def get(key: str, default=None):
        # UE_ROOT is the shared build/launcher override; MFPORT_UE_ROOT remains supported.
        env = os.environ.get('UE_ROOT') if key == 'ue_root' else None
        return env or os.environ.get(f'MFPORT_{key.upper()}', data.get(key, default))

    def path(key: str, default: str) -> Path:
        return native_path(str(get(key, default)), TOOL_DIR, system)

    uproject = path('uproject', str(PROJECT_DIR / 'DesktopForces.uproject'))
    configured_engine = get('ue_root', 'auto')
    ue_root = discover_engine(uproject) if configured_engine in ('', 'auto') else path('ue_root', 'auto')
    if ue_root is None:
        raise FileNotFoundError(f'No compatible Unreal Engine found for {uproject}. Set UE_ROOT or ue_root in [{system.lower()}] in mfport.toml.')
    return Config(
        game_dir=path('game_dir', '../../../MoFo/MobileForces'),
        work_dir=path('work_dir', 'work'),
        ue_root=ue_root,
        uproject=uproject,
        wine=str(get('wine', 'wine')),
        max_texture_size=int(get('max_texture_size', 512)),
    )
