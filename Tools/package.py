#!/usr/bin/env python3
"""Package Desktop Forces (UAT BuildCookRun) on Linux or Windows (Python 3.11+): a build that
starts on the main menu. On Windows use `python` (or `py -3`) instead of `python3` if needed.

    python3 package.py                          this machine's platform, Shipping
    python3 package.py Linux Development        with the console and logs
    python3 package.py Windows --zip            (on Windows) and one file to hand around
    python3 package.py Android Development --maps MF_TestRange,mf-warehouse
                                                an arm64 APK (ASTC textures) with two maps
    python3 package.py Windows --downloads      core and community in the build; unverified for
                                                the download server, classic apart (Docs/Building.md)
    python3 package.py Android --downloads      core pack alone in the APK; community too for the server
    python3 package.py --compile-only           build the editor and game code, no cook (CI)
    python3 package.py Linux --server           a dedicated server (Linux, Development; `Linux Shipping --server`
                                                for the release one: faster, no logs, no cheats): built in
                                                Epic's engine image, runs anywhere without it
    python3 package.py --dry-run Windows        print the commands instead of running them

Output: Saved/Packages/<Platform>-<Config>/ (every platform);
--downloads also writes <output>-downloads/Private/<Platform>/<content build id>/ (every pack the
build lacks, classic too: the team's) and Downloads/<Platform>/<content build id>/ (hard links to
those without classic: upload it).
Engine: as Tools/play.py (UE_ROOT, mfport.toml `ue_root`).

--zip names what's handed around, in Saved/Packages/<version>/, named as Unreal orders its own files
(DesktopForces-<Platform>[-Development]-<version>[-<Edition>].zip; Android: .apk):
    release     (no edition) --downloads: no Classic or Unverified, for anyone (GitHub releases);
                desktop: core and community, Android: core (a small APK); the rest downloads in game.
                Without --downloads the build has every pack: -Full
    team        -Team: every pack the release lacks, to unzip over it: the full game (for the team)
    classic, unverified, community
                -Classic ...: one of those packs, to unzip over it
    full        -Full: every pack in one file (not Android)
    python3 package.py Windows --downloads --zip release,team    a release and the team's add-on
A --server build makes DesktopForces-LinuxServer-<version>.tar.gz and -Team.tar.gz the same way
(--zip release,team unless told otherwise).

The classic plugin holds content converted from YOUR copy of Mobile Forces: a build with it is
for you and the team, never for sharing. Unverified (authors not asked yet) belongs to the modded
community release: the download server, never inside a shared build.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
from mfport.mflib.config import load, map_folders  # noqa: E402
from mfport.mflib.ue import editor_cmd, ubt_command, ubt_launch  # noqa: E402

WINDOWS = platform.system() == 'Windows'
CLASSIC_MARK = 'mobileforcesclassic'
UNVERIFIED_MARK = 'mobileforcesunverified'
# a project plugin's cooked content in a cooker list (lowercase): ...\Cooked\<Platform>\DesktopForces\Plugins\<Name>\...
PLUGIN_CONTENT = re.compile(r'[\\/]desktopforces[\\/]plugins[\\/]([^\\/]+)[\\/]')
# the cook platform folder (Saved/StagedBuilds, Saved/Cooked) where it isn't the platform's name
STAGE_NAMES = {'Android': 'Android_ASTC'}
# the name Unreal's build tools take (UnrealTargetPlatform); the game and the download server say Windows
UE_PLATFORMS = {'Windows': 'Win64'}


class Fail(Exception):
    pass



def hold_zen(ue: Path, host: str, dry: bool) -> None:
    """Keep the local Zen server (DDC and the cook's oplog) up for the whole run, owned by this
    script. UAT and the cook commandlet each start it owned by themselves when it isn't running;
    when the cook's copy exits with it, UAT's next request ("Reading oplog") goes to a fresh one
    and waits forever (2026-10-29, 2026-11-08). Attach to a running one, else start it as the
    engine does (its zenserver.runcontext)."""
    zen = ue / 'Engine/Binaries' / host / ('zen.exe' if WINDOWS else 'zen')
    common = Path(os.environ.get('LOCALAPPDATA', '')) if WINDOWS else Path.home() / '.config/Epic'
    context = common / 'UnrealEngine/Common/Zen/Install/zenserver.runcontext'
    if dry or not zen.exists():
        return
    owner = ['--owner-pid', str(os.getpid())]
    if subprocess.run([zen, 'attach', *owner], capture_output=True).returncode == 0:
        print(f'zen: held by this run (pid {os.getpid()})')
        return
    if not context.exists():
        print('zen: not running and no runcontext; UAT starts its own')
        return
    ctx = json.loads(context.read_text())
    subprocess.Popen([ctx['Executable'], *shlex.split(ctx['CommandlineArguments'], posix=not WINDOWS), *owner],
                     cwd=ctx.get('WorkingDirectory'), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                     start_new_session=not WINDOWS)
    print(f'zen: started for this run (pid {os.getpid()})')

def run(cmd: list, dry: bool, **kw) -> None:
    print('>', ' '.join(f'"{c}"' if ' ' in str(c) else str(c) for c in cmd), flush=True)
    if not dry and subprocess.call([str(c) for c in cmd], **kw) != 0:
        raise Fail(f'{Path(str(cmd[0])).name} failed')


# --- checks -------------------------------------------------------------------------------

def windows_toolchain(ue: Path) -> None:
    """MSVC, the Windows SDK the engine asks for and the .NET Framework SDK, before an hour of
    cooking fails on them."""
    missing = []
    pf86 = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'))
    vswhere = pf86 / 'Microsoft Visual Studio/Installer/vswhere.exe'
    vs = subprocess.run([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                         '-property', 'installationPath'], capture_output=True, text=True).stdout.strip() if vswhere.is_file() else ''
    if not vs:
        missing.append("the MSVC C++ compiler: workload 'Game development with C++' (or 'Desktop development with C++')")
    sdk_min, sdk_main = (10, 0, 19041, 0), '10.0.22621.0'
    sdk_json = ue / 'Engine/Config/Windows/Windows_SDK.json'
    if sdk_json.is_file():  # it has // comments: read the two lines needed
        text = sdk_json.read_text()
        if m := re.search(r'"MinVersion"\s*:\s*"([\d.]+)"', text):
            sdk_min = tuple(int(x) for x in m.group(1).split('.'))
        if m := re.search(r'"MainVersion"\s*:\s*"([\d.]+)"', text):
            sdk_main = m.group(1)
    include = pf86 / 'Windows Kits/10/Include'
    sdks = [d for d in include.glob('10.*') if (d / 'um/windows.h').is_file()
            and tuple(int(x) for x in d.name.split('.')) >= sdk_min] if include.is_dir() else []
    if not sdks:
        missing.append(f"a Windows SDK {'.'.join(map(str, sdk_min))} or newer: individual component 'Windows 11 SDK ({sdk_main})'")
    if not list((pf86 / 'Windows Kits/NETFXSDK').glob('4.*')):
        missing.append("the .NET Framework SDK: workload '.NET desktop development', component '.NET Framework 4.8 SDK'")
    if missing:
        raise Fail('Missing for building C++ (Visual Studio Installer -> Modify):\n  - ' + '\n  - '.join(missing))


def android_env() -> dict:
    """SDK / NDK / JDK from the environment, else where Android Studio or sdkmanager puts them;
    the versions are the engine's (Engine/Config/Android/Android_SDK.json)."""
    env = os.environ.copy()
    default = Path(os.environ.get('LOCALAPPDATA', '')) / 'Android/Sdk' if WINDOWS else Path.home() / 'Android/Sdk'
    sdk = Path(env.setdefault('ANDROID_HOME', str(default)))
    if not env.get('NDKROOT'):
        ndks = sorted((sdk / 'ndk').glob('*'), key=lambda p: [int(x) for x in re.findall(r'\d+', p.name)])
        if ndks:
            env['NDKROOT'] = str(ndks[-1])
    env['NDK_ROOT'] = env.get('NDKROOT', '')
    if not env.get('JAVA_HOME') and (javac := shutil.which('javac')):
        env['JAVA_HOME'] = str(Path(javac).resolve().parent.parent)
    if not (sdk / 'platforms').is_dir() or not env['NDK_ROOT'] or not env.get('JAVA_HOME'):
        raise Fail('Android needs the SDK, NDK and a JDK: see Docs/Building.md (Android)')
    return env


def find_maps(project: Path, only: str | None) -> list[str]:
    """Every built map (the cooker doesn't take maps from the always-cook folders), or those named in
    <only> (comma-separated). A pack plugin's (Plugins/<Name>/Content) maps are cooked by name; every
    other folder of it must be always cooked (content loaded by path at run time: DefaultGame.ini),
    Maps not (a map's data and lightmaps come with the map, so --maps doesn't cook other maps')."""
    ini = (project / 'Config/DefaultGame.ini').read_text()
    for content in sorted(project.glob('Plugins/*/Content')):
        for folder in sorted(d.name for d in content.iterdir() if d.is_dir() and d.name != 'Maps'):
            line = f'+DirectoriesToAlwaysCook=(Path="/{content.parent.name}/{folder}")'
            if line not in ini:
                raise Fail(f'Config/DefaultGame.ini: add {line}')
    names = set(filter(None, (only or '').split(',')))
    return [f'{mount}/{m.stem}' for folder, mount in map_folders(project) for m in sorted(folder.glob('*.umap'))
            if not names or m.stem in names]


# --- downloads ------------------------------------------------------------------------------

def chunk_of(name: str) -> int | None:
    m = re.match(r'pakchunk(\d+)', name)
    return int(m.group(1)) if m else None


def manifest_line(path: Path, chunk: int) -> str:
    """ChunkDownloader's manifest line: name <tab> size <tab> version (SHA1:, checked after
    download) <tab> chunk <tab> url (relative to the manifest)."""
    sha1 = hashlib.sha1()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            sha1.update(block)
    return f'{path.name}\t{path.stat().st_size}\tSHA1:{sha1.hexdigest().upper()}\t{chunk}\t{path.name}'


def write_manifest(folder: Path, lines: list[str], build_id: str, platform_name: str) -> None:
    manifest = [f'$NUM_ENTRIES = {len(lines)}', f'$BUILD_ID = {build_id}', *lines]
    (folder / f'BuildManifest-{platform_name}.txt').write_text('\n'.join(manifest) + '\n', newline='\n')


def pack_kinds(chunks: set[int], chunk_lists: Path) -> dict[int, str]:
    """'classic', 'unverified' or 'community' for each chunk, from what the cooker put in it
    (pakchunkN.txt): classic wins over unverified; community (shared in a release) only with no
    project plugin's content but the unverified one, so content of a new plugin stops the build
    instead of being shared. No chunk ids are listed here: a new pack needs no change."""
    kinds = {}
    for n in sorted(chunks):
        listing = chunk_lists / f'pakchunk{n}.txt'
        if not listing.is_file():
            raise Fail(f'no cooker list for pakchunk{n}: {listing}')
        text = listing.read_text(errors='replace').lower()
        if CLASSIC_MARK in text:
            kinds[n] = 'classic'
        elif unknown := set(PLUGIN_CONTENT.findall(text)) - {UNVERIFIED_MARK}:
            raise Fail(f'pakchunk{n} holds content of {", ".join(sorted(unknown))}: neither classic nor unverified ({listing})')
        else:
            kinds[n] = 'unverified' if UNVERIFIED_MARK in text else 'community'
    return kinds


def pack_files(paks: Path, chunk_lists: Path) -> dict[Path, str]:
    """Every pack file in <paks> but core's, with its pack's kind (pack_kinds). Stops when core
    (chunk 0, in every build) holds classic or unverified content - a missing pack label, or a
    reference that pulled an asset in - before anything is moved or shared."""
    files = sorted(p for p in paks.glob('pakchunk*') if chunk_of(p.name) is not None)
    kinds = pack_kinds({0} | {chunk_of(p.name) for p in files}, chunk_lists)
    if kinds[0] != 'community':
        raise Fail(f'the core pak holds {kinds[0]} content, and every build ships it: {chunk_lists / "pakchunk0.txt"}')
    return {p: kinds[chunk_of(p.name)] for p in files if chunk_of(p.name) != 0}


def addon(packs: dict[Path, str], what: str) -> dict[Path, str]:
    """The packs of --zip <what> (an add-on): team is every one, classic / unverified / community one kind."""
    return {p: kind for p, kind in packs.items() if what in ('team', kind)}


def link(src: Path, dest: Path) -> None:
    """dest as a hard link to src: no extra space; a copy where the file system has no hard links."""
    dest.unlink(missing_ok=True)
    try:
        os.link(src, dest)
    except OSError:
        shutil.copy2(src, dest)


def split_downloads(paks: Path, chunk_lists: Path, out: Path, build_id: str, platform_name: str,
                    keep_community: bool = False) -> dict[Path, str]:
    """Move the packs other than core (chunk 0) out of the staged build - with keep_community, the
    community ones stay in it - to <out>/Private/<platform>/<build id>/ (every pack the build lacks,
    classic too: the team's) and hard link those without classic content - converted from the
    player's own Mobile Forces, never on a public server - into <out>/Downloads/<platform>/<build id>/
    (the download server's); each folder with ChunkDownloader's BuildManifest-<platform>.txt (the
    game asks <server>/<platform>/<build id>/). Returns each file moved, with its pack's kind."""
    if not all((build_id, platform_name)):
        raise Fail('split_downloads: empty build id or platform')
    # decided before moving anything, from what the cooker put in each chunk
    packs = {p: kind for p, kind in pack_files(paks, chunk_lists).items() if not (keep_community and kind == 'community')}
    upload, private = out / 'Downloads' / platform_name / build_id, out / 'Private' / platform_name / build_id
    for d in (out / 'Downloads', out / 'Private'):
        shutil.rmtree(d, ignore_errors=True)
    for d in (upload, private):
        d.mkdir(parents=True)
    moved, lines, all_lines = {}, [], []
    for p, kind in packs.items():
        dest = private / p.name
        shutil.move(p, dest)
        moved[dest] = kind
        line = manifest_line(dest, chunk_of(p.name))
        all_lines.append(line)
        if kind != 'classic':
            link(dest, upload / p.name)
            lines.append(line)
    write_manifest(upload, lines, build_id, platform_name)
    write_manifest(private, all_lines, build_id, platform_name)
    return moved


# where a pack dropped in by hand is mounted at start: the install's own Content/Paks on the
# desktop; Saved/Paks in the app's storage on Android (the APK holds Content/Paks)
ANDROID_PAKS = 'Android/data/com.fistodul.desktopforces/files/UnrealGame/DesktopForces/DesktopForces/Saved/Paks'
# stored, not deflated, in a zip: iostore containers and packages are compressed already
STORED = ('.pak', '.ucas', '.utoc', '.obb', '.apk')


def share_name(platform_name: str, config: str, version: str, edition: str = '') -> str:
    """What a build is called wherever it's shared. The order is Unreal's, whose names can't change
    (DesktopForces-Android-Shipping-arm64.apk, pakchunk1-Windows.pak); Shipping goes unsaid."""
    return '-'.join(filter(None, ('DesktopForces', platform_name, '' if config == 'Shipping' else config, version, edition)))


# --zip: what to share, and the edition each is named with. An add-on is packs the release lacks, to
# unzip over it: team is all of them (the release becomes the full game), the others one kind
ZIPS = {'release': '', 'full': 'Full', 'team': 'Team', 'classic': 'Classic', 'unverified': 'Unverified', 'community': 'Community'}
ADDONS = ('team', 'classic', 'unverified', 'community')


def share_path(release: Path, what: str) -> Path:
    """Where --zip <what> goes, made if missing: Saved/Packages/<version>/ holds only what anyone may
    have (the release, the community pack); team/ the rest, with classic or unverified content."""
    folder = release if what in ('release', 'community') else release / 'team'
    folder.mkdir(parents=True, exist_ok=True)
    return folder


def build_zip(archive: Path, out: Path, packs: list[Path] = (), paks_dir: str = '') -> Path:
    """<archive>: the build folder <out> (as <out.name>/...), with <packs> added under <paks_dir>,
    where the game mounts them at start. Containers are stored (STORED): deflating GBs of
    compressed paks took minutes for under 1%."""
    files = [(p, f'{out.name}/{p.relative_to(out).as_posix()}') for p in sorted(out.rglob('*')) if p.is_file()]
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for p, member in [*files, *((p, f'{paks_dir}/{p.name}') for p in packs)]:
            z.write(p, member, zipfile.ZIP_STORED if p.suffix.lower() in STORED else zipfile.ZIP_DEFLATED)
    return archive


def addon_zip(packs: dict[Path, str], platform_name: str, build_id: str, archive: Path, edition: str) -> Path:
    """<archive>: the pack files <packs> (file: kind) laid out to be unzipped over the release's
    install (desktop: the folder with the game's executable; Android: the phone's storage root, from
    a computer over USB). With classic inside, for the team only."""
    where, target = ((ANDROID_PAKS, "the phone's storage (over USB from a computer)") if platform_name == 'Android' else
                     ('DesktopForces/Content/Paks', 'the folder with the game (DesktopForces.exe / DesktopForces.sh)'))
    kinds = sorted(set(packs.values()))
    readme = (f'Desktop Forces packs for {platform_name} build {build_id}: {", ".join(kinds)}.\n'
              + ('Classic is converted from the original Mobile Forces: for the team only, never share it publicly.\n' if 'classic' in kinds else '')
              + f'\nUnzip into {target}; the files land in {where}/.\n'
              f'They only work with game build {build_id} (Downloads page). Remove the files to drop a pack.\n')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_STORED) as z:  # iostore containers are compressed already
        z.writestr(f'README-{edition}.txt', readme)
        for p in sorted(packs):
            z.write(p, f'{where}/{p.name}')
    return archive


def server_archives(out: Path, paks: Path, chunk_lists: Path, wanted: dict[str, Path]) -> list[Path]:
    """Tarballs of the server folder <out> (every pack), for each wanted --zip (its path without
    .tar.gz): release without the unverified and classic packs (what anyone may host), full with
    every pack, team / classic / unverified those packs alone, unpacked over the release (in the
    folder holding it). With classic inside, for the team only."""
    extra = {p: kind for p, kind in pack_files(paks, chunk_lists).items() if kind != 'community'}
    member = lambda p: f'{out.name}/{p.relative_to(out).as_posix()}'  # noqa: E731
    made = []
    for what, base in wanted.items():
        archive = base.with_name(base.name + '.tar.gz')
        picked = addon(extra, what) if what in ADDONS else {}
        if what in ADDONS and not picked:
            print(f'  no {what} pack in this server: no {archive.name}')
            continue
        # level 1: the paks are compressed already, a higher level only costs time
        with tarfile.open(archive, 'w:gz', compresslevel=1) as t:
            if picked:
                for p in sorted(picked):
                    t.add(p, member(p))
            else:
                skip = {member(p) for p in extra} if what == 'release' else set()
                t.add(out, out.name, filter=lambda info: None if info.name in skip else info)
        made.append(archive)
    return made


def game_version(project: Path) -> str:
    """The version a build carries (GeneralProjectSettings.ProjectVersion): its commit's date,
    `20261030`, as releases are named. UE lets a client join a server only with the same
    ProjectVersion, so builds of another day are refused instead of desyncing; builds of one day
    play together on every platform (the commit's date: building it later doesn't change it)."""
    stamp = subprocess.run(['git', '-C', str(project), 'log', '-1', '--format=%cd', '--date=format:%Y%m%d'], capture_output=True, text=True).stdout.strip()
    return stamp or 'local'


def with_project_version(ini: str, version: str) -> str:
    """DefaultGame.ini text with [/Script/EngineSettings.GeneralProjectSettings] ProjectVersion set."""
    text, n = re.subn(r'(?m)^ProjectVersion=[^\r\n]*', f'ProjectVersion={version}', ini)
    if n != 1:
        raise Fail('DefaultGame.ini: expected one ProjectVersion= line')
    return text


# --- dedicated server ---------------------------------------------------------------------

# Server targets need an engine built from source; the launcher engine has none. Epic's image is
# one (GitHub account linked to Epic, `docker login ghcr.io`), used only to build: the server it
# makes is a plain folder that runs on any x86-64 Linux, no container (Docs/Hosting.md).
SERVER_IMAGE = os.environ.get('DF_SERVER_IMAGE', 'ghcr.io/epicgames/unreal-engine:dev-slim-5.8.4')
SERVER_TIMEOUT = '6h'  # clang once crash-looped for 2 hours in the container without ending the build
# not copied into the build's project: this host's build products, caches and the converter's work
SERVER_SKIP = ['/Binaries/', '/Intermediate/', '/Saved/', '/DerivedDataCache/', '/Build/', '/.git/', '/Tools/mfport/work/']


def server_uat(maps: list[str], ue_home: str, config: str = 'Development') -> list[str]:
    """BuildCookRun in the image for a server of `maps`, the project mounted at /project."""
    uat = [f'{ue_home}/UnrealEngine/Engine/Build/BatchFiles/RunUAT.sh', 'BuildCookRun', '-project=/project/DesktopForces.uproject',
           '-noP4', '-utf8output', '-unattended', '-ubtargs=-NoUBA', '-server', '-noclient', '-serverplatform=Linux',
           f'-serverconfig={config}', '-build', '-cook', '-stage', '-pak', '-iostore', '-compressed', '-archive',
           '-archivedirectory=/project/Saved/Server']
    if maps:
        uat.append(f'-map={"+".join(maps)}')
    return uat


def package_server(a: argparse.Namespace) -> None:
    """A dedicated server: the project copied to a build tree (this checkout's own builds, caches
    and Zen untouched), built and cooked in Epic's engine image, archived to Saved/Packages/LinuxServer/
    (every pack) and packed as --zip says (server_archives): by default the release,
    Saved/Packages/<version>/DesktopForces-LinuxServer-<version>.tar.gz (no unverified or classic
    pack), and -Team.tar.gz (those two)."""
    if WINDOWS:
        raise Fail('--server builds on Linux (docker); the server it makes runs on Linux')
    cfg = load()
    project = cfg.uproject.parent
    maps = find_maps(project, a.maps)
    version = a.version or game_version(project)  # a client joins only a server of its own ProjectVersion
    tree = Path(os.environ.get('DF_SERVER_TREE', Path.home() / '.cache/desktopforces-server')) / 'project'
    out = project / f'Saved/Packages/LinuxServer{"" if a.config == "Development" else "-" + a.config}'
    print(f'Packaging the Linux server ({a.config}) {version} -> {out} ({len(maps)} maps)')
    if a.dry_run:
        print('(dry run)')
    else:
        tree.mkdir(parents=True, exist_ok=True)
    # rsync keeps the tree's own Binaries / Intermediate / DDC between builds (P = protect)
    rules = [f'--filter=P {d}' for d in ('/Binaries/', '/Intermediate/', '/DerivedDataCache/', '/Saved/')]
    run(['rsync', '-a', '--delete', *rules, *(f'--exclude={d}' for d in SERVER_SKIP), f'{project}/', f'{tree}/'], a.dry_run)
    if not a.dry_run:
        ini = tree / 'Config/DefaultGame.ini'
        ini.write_text(with_project_version(ini.read_text(encoding='utf-8'), version), encoding='utf-8')
        (tree / 'Intermediate/CachedAssetRegistryDiscovery.bin').unlink(missing_ok=True)
        shutil.rmtree(tree / 'Saved/Server', ignore_errors=True)
    home = '/home/ue4'
    run(['timeout', SERVER_TIMEOUT, 'docker', 'run', '--rm', '-u', f'{os.getuid()}:{os.getgid()}', '-v', f'{tree}:/project',
         '-e', f'HOME={home}', SERVER_IMAGE, *server_uat(maps, home, a.config)], a.dry_run)
    if a.dry_run:
        return
    built = tree / 'Saved/Server/LinuxServer'
    if not (built / 'DesktopForcesServer.sh').exists():
        raise Fail(f'no server in {built}')
    # moved, not copied: the tree's staged and archived builds are made anew by the next build
    shutil.rmtree(out, ignore_errors=True)
    shutil.move(built, out)
    shutil.rmtree(tree / 'Saved/StagedBuilds', ignore_errors=True)
    release = project / 'Saved/Packages' / version
    release.mkdir(parents=True, exist_ok=True)
    stem = cfg.uproject.stem
    archives = server_archives(out, out / stem / 'Content/Paks', tree / 'Saved/Cooked/LinuxServer' / stem / 'Metadata/ChunkManifest',
                               {what: share_path(release, what) / share_name('LinuxServer', a.config, version, ZIPS[what]) for what in a.zip})
    print(f'Done: {out}, version {version}; to host: unpack the release (or full) tarball anywhere, the add-ons over it, '
          f'and run {out.name}/DesktopForcesServer.sh (Docs/Hosting.md)', *archives, sep='\n  ')


# --- the build ------------------------------------------------------------------------------

def package(a: argparse.Namespace) -> None:
    cfg = load()
    ue, uproject = cfg.ue_root, cfg.uproject
    project = uproject.parent
    host = 'Win64' if WINDOWS else 'Linux'
    batch = ue / 'Engine/Build/BatchFiles'
    if WINDOWS and not a.dry_run:
        windows_toolchain(ue)
    if a.clean and not a.dry_run:
        for d in ('Binaries', 'Intermediate/Build'):
            shutil.rmtree(project / d / host, ignore_errors=True)

    # editor modules (the cooker and the pack labels run them), as Tools/play.py builds them (with
    # this engine: no second search for it)
    run([sys.executable, TOOLS / 'play.py', 'build'], a.dry_run, env={**os.environ, 'UE_ROOT': str(ue)})
    if a.compile_only:  # the game's modules, as play.py builds the editor's
        build = ubt_command(ue, WINDOWS, 'DesktopForces', UE_PLATFORMS.get(a.platform, a.platform), a.config, uproject)
        cwd, env = ubt_launch(ue, build, project)
        run(build, a.dry_run, cwd=cwd, env=env)
        return

    out = project / f'Saved/Packages/{a.platform}-{a.config}'
    downloads_out = out.with_name(out.name + '-downloads')
    env = android_env() if a.platform == 'Android' else os.environ.copy()

    # the asset registry's cached folder listing can outlive content moved or copied in (Repack, a
    # sync), and the cooker then can't find packages that are on disk: rescan, it takes seconds
    if not a.dry_run:
        (project / 'Intermediate/CachedAssetRegistryDiscovery.bin').unlink(missing_ok=True)

    # content packs -> their own paks (bGenerateChunks): label each pack root (cheap once they exist)
    labels_log = project / 'Saved/Logs/PackLabels.log'
    labels_log.parent.mkdir(parents=True, exist_ok=True)
    run([editor_cmd(cfg), uproject, '-run=DFImport', '-steps=packs', '-unattended', '-nopause', '-nosplash', f'-abslog={labels_log}'],
        a.dry_run, stdout=subprocess.DEVNULL)
    if not a.dry_run and 'duplicate PrimaryAssetID' in labels_log.read_text(errors='replace'):
        # only one of two same-named labels counts: the other's folder would fall into the core pak
        raise Fail(f'two pack labels share a name: {labels_log}')

    # content folders differing only in case (a Linux checkout allows them): UE names packages
    # case-insensitively, so one twin hides the other's packages, and Windows can't hold both
    seen = {}
    for d in [*project.glob('Content/**/'), *project.glob('Plugins/*/Content/**/')]:
        if seen.setdefault(str(d).lower(), d) != d:
            raise Fail(f'content folders that differ only in case: {seen[str(d).lower()]}, {d}')

    maps = find_maps(project, a.maps)

    uat = [batch / ('RunUAT.bat' if WINDOWS else 'RunUAT.sh'), 'BuildCookRun', f'-project={uproject}', '-noP4', '-utf8output',
           '-unattended', f'-platform={UE_PLATFORMS.get(a.platform, a.platform)}', f'-clientconfig={a.config}']
    paks = ['-pak', '-iostore', '-compressed']  # not for the cook: UAT paks only what it stages
    if a.config == 'Shipping':
        uat.append('-nodebuginfo')
    if a.platform == 'Android':
        uat.append('-cookflavor=ASTC')
    package_args = []
    if a.platform == 'Android':
        package_args.append('-package')  # the APK
        if a.downloads:
            # core alone is small: in the APK, one file to install (an OBB needs adb or a file
            # manager allowed into Android/obb); a build with every pack keeps the OBB (2 GB APK limit)
            package_args.append('-forcepackagedata')
    map_arg = [f'-map={"+".join(maps)}'] if maps else []
    # Cook and stage in two UAT runs: staging reads the cook's oplog from Zen over an HTTP client
    # made before the cook, whose pooled connections sat idle through it and are dead by then -
    # the request waits out its 20 min timeout ("Reading oplog", 2026-11-08). A new process connects
    # afresh
    cook = [*uat, '-ubtargs=-NoUBA', '-build', '-cook', *map_arg]
    staging = [*uat, *paks, '-skipcook', '-stage', '-prereqs', *map_arg]

    build_id_file = project / 'Content/Downloads/ContentBuildId.txt'
    version = a.version or game_version(project)  # before anything below changes the project
    release = project / 'Saved/Packages' / version
    stage_name = STAGE_NAMES.get(a.platform, a.platform)
    stage = project / 'Saved/StagedBuilds' / stage_name
    packs = {}  # --downloads: the pack files taken out of the build, with their kinds
    game_ini = project / 'Config/DefaultGame.ini'
    game_ini_bytes = game_ini.read_bytes()  # restored byte for byte (line endings too)
    print(f'Packaging {a.platform} {a.config} {version} -> {out} ({len(maps)} maps)')
    if a.dry_run:
        print('(dry run)')
    hold_zen(ue, host, a.dry_run)
    try:
        # the content build id (UDFDownloads): only in a --downloads build, only while it's made -
        # the paks on the server must come from the same cook as the game asking for them
        build_id = ''
        if a.downloads:
            commit = subprocess.run(['git', '-C', str(project), 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True).stdout.strip()
            build_id = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d-%H%M%S') + '-' + (commit or 'local')
        if not a.dry_run:
            build_id_file.parent.mkdir(parents=True, exist_ok=True)  # staged as UFS (DefaultGame.ini)
            build_id_file.unlink(missing_ok=True)
            if build_id:
                build_id_file.write_text(build_id)
            shutil.rmtree(out, ignore_errors=True)  # paks from an earlier build would be mounted too
            for obb in stage.parent.glob(f'{stage_name}*.obb'):  # else an earlier OBB, every pack in it, is reused
                obb.unlink()
            game_ini.write_bytes(with_project_version(game_ini_bytes.decode('utf-8'), version).encode('utf-8'))
        run(cook, a.dry_run, env=env)
        if not a.downloads:
            run([*staging, '-archive', f'-archivedirectory={out}', *package_args], a.dry_run, env=env)
        else:
            # stage, take the packs out of the staged build (desktop keeps community; the APK holds
            # core alone, kept small), then package / archive what's left
            run(staging, a.dry_run, env=env)
            if not a.dry_run:
                packs = split_downloads(stage / uproject.stem / 'Content/Paks', project / 'Saved/Cooked' / stage_name / uproject.stem / 'Metadata/ChunkManifest',
                                        downloads_out, build_id, a.platform, keep_community=a.platform != 'Android')
            run([*uat, *paks, '-skipcook', '-skipstage', '-skippak', '-archive', f'-archivedirectory={out}', *package_args], a.dry_run, env=env)
        if not a.dry_run:
            # the staged build is a second copy of <out> (with every pack when not --downloads) and
            # the next staging makes it anew
            shutil.rmtree(stage, ignore_errors=True)
            for obb in stage.parent.glob(f'{stage_name}*.obb'):
                obb.unlink()
    finally:
        if not a.dry_run:
            build_id_file.unlink(missing_ok=True)
            game_ini.write_bytes(game_ini_bytes)

    made = {}  # --zip <what>: the file
    if a.zip and not a.dry_run:
        def name(what: str, suffix: str = '.zip') -> Path:
            return share_path(release, what) / (share_name(a.platform, a.config, version, ZIPS[what]) + suffix)
        if not a.downloads:  # the build itself has every pack (with --maps, some of their maps): one zip, never the release's name
            made['full'] = build_zip(name('full'), out)
        elif a.platform == 'Android':
            if 'release' in a.zip:  # one file: the APK holds the core pack
                apks = list(out.glob('DesktopForces*.apk'))  # not AFS_*, Unreal's file-server helper
                if len(apks) != 1:
                    raise Fail(f'expected one APK in {out}: {apks}')
                made['release'] = name('release', '.apk')
                link(apks[0], made['release'])
        else:
            if 'release' in a.zip:
                made['release'] = build_zip(name('release'), out)
            if 'full' in a.zip:
                made['full'] = build_zip(name('full'), out, sorted(packs), f'{out.name}/{uproject.stem}/Content/Paks')
        for what in (w for w in ADDONS if w in a.zip):
            if picked := addon(packs, what):
                made[what] = addon_zip(picked, a.platform, build_id, name(what), ZIPS[what])
            else:
                print(f'  no {what} pack outside this build: no {name(what).name}')
        print(f'In {release}:', *(str(p.relative_to(release)) for p in made.values()), sep='\n  ')
        if outputs := os.environ.get('GITHUB_OUTPUT'):  # a CI job uploads what was made, by name
            with open(outputs, 'a', encoding='utf-8') as f:
                f.writelines(f'{what}={p}\n' for what, p in made.items())
    print(f'Done: {out}, version {version}' + (f' (content build {build_id})' if build_id else ''))
    if a.downloads:
        print(f'  to the download server: bash Tools/upload_downloads.sh {a.platform} {build_id} (on Windows in WSL; Docs/Building.md)')
        print(f'  {downloads_out}/Private/: every pack the build lacks, classic too - for the team (a team server), never a public one')


PLATFORMS, CONFIGS = ['Linux', 'Windows', 'Android'], ['Shipping', 'Development']


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('platform', nargs='?', choices=PLATFORMS, default='Windows' if WINDOWS else 'Linux')
    ap.add_argument('config', nargs='?', choices=CONFIGS, help='default: Shipping for the game, Development for --server')
    ap.add_argument('--downloads', action='store_true', help='no Classic or Unverified pack in the build (Android: core alone): '
                                                             'those for the server and the team')
    ap.add_argument('--maps', help='comma-separated map names (default: every built map)')
    ap.add_argument('--zip', nargs='?', const='release', metavar='WHAT',
                    help=f'files to share (above), comma-separated: {", ".join(ZIPS)}; alone: release; --server: release,team')
    ap.add_argument('--compile-only', action='store_true', help='build the editor and game code, no cook')
    ap.add_argument('--clean', action='store_true', help="delete this host's Binaries / Intermediate/Build first")
    ap.add_argument('--dry-run', action='store_true', help='print the commands instead of running them')
    ap.add_argument('--server', action='store_true', help='a dedicated Linux server instead of the game (Development unless Shipping is given)')
    ap.add_argument('--version', help='the network version (ProjectVersion, yyyymmdd) instead of the commit date: '
                                      'a content-only rebuild that must still play with builds already out')
    # a bare --zip before the platform (`--zip Windows`, fine before --zip took a value) means release
    argv = ['--zip=release' if x == '--zip' and argv[i + 1:i + 2] and argv[i + 1] in PLATFORMS + CONFIGS else x for i, x in enumerate(argv)]
    a = ap.parse_args(argv)
    if a.config is None:
        a.config = 'Development' if a.server else 'Shipping'
    a.zip = [w for w in (a.zip or ('release,team' if a.server else '')).split(',') if w]
    if unknown := set(a.zip) - set(ZIPS):
        ap.error(f'--zip: unknown {", ".join(sorted(unknown))} (one of {", ".join(ZIPS)})')
    if set(a.zip) - {'release'} and not (a.server or a.downloads):
        ap.error('--zip: without --downloads the build has every pack: --zip alone')
    if 'full' in a.zip and a.platform == 'Android' and not a.server:
        ap.error('--zip full: not on Android (an APK stops at 2 GB); release,team gives the same')
    if 'community' in a.zip and a.platform != 'Android':
        ap.error('--zip community: only Android leaves it out of the release')
    if a.server and (a.platform != 'Linux' or a.downloads or a.compile_only):
        ap.error('--server: Linux only, without --downloads / --compile-only')
    try:
        package_server(a) if a.server else package(a)
    except (Fail, OSError) as exc:
        print(f'package: {exc}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
