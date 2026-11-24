"""Run Unreal Editor commandlets for the import / lighting steps."""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import threading
from pathlib import Path

from .config import Config


def windows_ubt_prefix(root: Path) -> list[str]:
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


def ubt_command(root: Path, windows: bool, target: str, platform: str, config: str, uproject: Path, *extra: str) -> list[str]:
    """UnrealBuildTool building <target> (the editor for play.py, the game for package.py): on a
    Windows host straight through the engine's .NET runtime, on Linux Build.sh. Run it as
    ubt_launch says."""
    prefix = windows_ubt_prefix(root) if windows else [str(root / 'Engine/Build/BatchFiles/Linux/Build.sh')]
    return [*prefix, target, platform, config, f'-project={uproject}', '-waitmutex', '-NoUBA', *extra]


def ubt_launch(root: Path, cmd: list[str], project_dir: Path) -> tuple[Path, dict]:
    """The folder and environment to run <cmd> in: UBT run by its .NET runtime (ubt_command on
    Windows) starts in Engine/Source with that runtime first, as Build.bat sets it up."""
    env = os.environ.copy()
    if not cmd[1:2] or not str(cmd[1]).endswith('UnrealBuildTool.dll'):
        return project_dir, env
    if env.get('UE_USE_SYSTEM_DOTNET') != '1':
        env.update(DOTNET_ROOT=str(Path(cmd[0]).parent), DOTNET_MULTILEVEL_LOOKUP='0', DOTNET_ROLL_FORWARD='LatestMajor')
        env['PATH'] = env['DOTNET_ROOT'] + os.pathsep + env.get('PATH', '')
    return root / 'Engine/Source', env


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


def run_importer(cfg: Config, steps: str = 'art,textures,props,sounds,music,maps', maps: list[str] | None = None) -> int:
    args = [f'-steps={steps}']
    default_out = (cfg.uproject.parent / 'Tools' / 'mfport' / 'work' / 'out').resolve()
    if cfg.out_dir.resolve() != default_out:
        # UnixCommonStartup adds its own quotes around argv values containing spaces.
        # Literal quotes here become doubled, so FParse reads an empty work directory.
        args.append(f'-work="{cfg.out_dir}"' if os.name == 'nt' else f'-work={cfg.out_dir}')
    if maps:
        args.append('-maps=' + ','.join(maps))
    return run_commandlet(cfg, 'DFImport', *args)


def map_package(cfg: Config, name: str) -> tuple[str, Path]:
    """Long package name and .umap file of a map: our own (MF_*) in /Game/MF/Maps, cleared
    community maps in /Game/Maps, retail ones in the MobileForcesClassic plugin, other custom
    maps in MobileForcesUnverified (packs.toml)."""
    root = cfg.uproject.parent
    if name.startswith('MF_'):
        return f'/Game/MF/Maps/{name}', root / 'Content' / 'MF' / 'Maps' / f'{name}.umap'
    from . import packs
    pack = packs.map_pack(packs.load(), name)
    if pack == 'community':
        return f'/Game/Maps/{name}', root / 'Content' / 'Maps' / f'{name}.umap'
    plugin = 'MobileForcesUnverified' if pack == 'unverified' else 'MobileForcesClassic'
    return f'/{plugin}/Maps/{name}', root / 'Plugins' / plugin / 'Content' / 'Maps' / f'{name}.umap'


def build_lighting(cfg: Config, maps: list[str], quality: str | None = None) -> int:
    """Bake static lighting (Lightmass) + precomputed visibility for generated maps, at `quality`
    or else the map's own (map_edits.toml `quality`, default Preview)."""
    from . import mapedits
    rc = 0
    for m in maps:
        pkg, path = map_package(cfg, m)
        if not path.exists():
            print(f'! no map {pkg} - import it first')
            rc = 1
            continue
        # -messaging: commandlets keep UdpMessaging off, and Lightmass talks to the editor
        # through it (without: "Timed out waiting for the recipient", then Lightmass dies)
        # no -BuildNavigationData: its rebuild comes out empty (tile size 0) and would replace
        # the navmesh the importer built (with ladder link fix-ups); resaving keeps that one
        # instanced props' bounds: the DesktopForcesEditor module refreshes them when the build
        # starts (OnLightingBuildStarted); compiling meshes synchronously instead registers
        # components in parallel, which asserts in UBoxComponent::CreateSceneProxy
        rc |= run_commandlet(cfg, 'ResavePackages', '-buildlighting', f'-quality={quality or mapedits.quality(m)}',
                             '-allowcommandletrendering', '-messaging', f'-map={pkg}',
                             watch=('Lightmass crashed', 'Assertion failed', 'Lighting build failed',
                                    'Failed to build lighting'))
    return rc


def repair_lightmass(cfg: Config) -> int:
    """Apply the verified Linux 5.8.4 Lightmass startup workaround, retaining the original.

    Only the bad static-init thread-tag assertion branch is removed. This is a local
    engine-tool workaround, not a runtime/game patch. Refuse every unverified build.
    """
    import hashlib
    import shutil

    library = cfg.ue_root / 'Engine/Binaries/Linux/libUnrealLightmass-Core.so'
    original_hash = 'fd45844f90b3bdcddee93f0d99727abff3e54652d9553d80d337b7be3911f74d'
    offset = 0x91BCF1  # FTaskTagScope ctor VA 0x91CCF1 minus ELF LOAD bias 0x1000
    expected = bytes.fromhex('0f853f010000')
    data = bytearray(library.read_bytes())
    backup = library.with_suffix('.so.orig')
    if data[offset:offset + 6] == b'\x90' * 6:
        restored = data[:]
        restored[offset:offset + 6] = expected
        if hashlib.sha256(restored).hexdigest() == original_hash:
            print(f'Lightmass workaround already applied: {library}')
            return 0
    if hashlib.sha256(data).hexdigest() != original_hash or data[offset:offset + 6] != expected:
        raise RuntimeError('Unverified Lightmass build; refusing to patch it.')
    if backup.exists() and hashlib.sha256(backup.read_bytes()).hexdigest() != original_hash:
        raise RuntimeError(f'Backup does not match this build: {backup}')
    if not backup.exists():
        shutil.copy2(library, backup)
    data[offset:offset + 6] = b'\x90' * 6
    temporary = library.with_suffix('.so.patched')
    shutil.copy2(library, temporary)
    temporary.write_bytes(data)
    temporary.replace(library)
    print(f'Repaired {library}; undo by restoring {backup}')
    return 0
