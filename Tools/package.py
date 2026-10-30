#!/usr/bin/env python3
"""Package Desktop Forces (UAT BuildCookRun) on Linux or Windows (Python 3.11+): a build that
starts on the main menu. On Windows use `py -3` instead of `python3` if needed.

    python3 Tools/package.py                          this machine's platform, Shipping
    python3 Tools/package.py Linux Development        with the console and logs
    python3 Tools/package.py Windows --zip            (on Windows) and one .zip to hand around
    python3 Tools/package.py --public                 without the Classic and Unverified plugins
    python3 Tools/package.py Android Development --maps MF_TestRange,mf-warehouse
                                                      an arm64 APK (ASTC textures) with two maps
    python3 Tools/package.py Android --downloads      core pack in the build; the other packs for
                                                      the download server (Docs/Building.md)
    python3 Tools/package.py --compile-only           build the editor and game code, no cook (CI)
    python3 Tools/package.py --dry-run Windows        print the commands instead of running them

Output: Saved/Packages/<Platform>-<Config>[-public]/ (every platform);
--downloads also writes <output>-downloads/Downloads/<Platform>/<content build id>/ (upload it)
and <output>-downloads/Sideload/ (the classic pack: never upload it); with --team also
Private/<Platform>/<content build id>/ (every pack, classic too: for a team server only) and
DesktopForces-Classic-<Platform>-<version>.zip (the classic pack, to unzip over a team member's
install). Engine: as Tools/play.py (UE_ROOT, mfport.toml `ue_root`).

The classic plugin holds content converted from YOUR copy of Mobile Forces: a build with it is
for you and the team, never for sharing; so is Unverified in a build handed around. --public
leaves both out.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
from mfport.mflib.config import load  # noqa: E402

WINDOWS = platform.system() == 'Windows'
CLASSIC_MARK = 'mobileforcesclassic'
# cook platform folder (Saved/StagedBuilds, Saved/Cooked) and ChunkDownloader's platform name
STAGE_NAMES = {'Android': ('Android_ASTC', 'Android'), 'Windows': ('Windows', 'Windows'), 'Linux': ('Linux', 'Linux')}
# the name Unreal's build tools take (UnrealTargetPlatform); the game and the download server say Windows
UE_PLATFORMS = {'Windows': 'Win64'}


class Fail(Exception):
    pass


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


def pack_plugins(project: Path, public: bool) -> list[str]:
    """The pack plugins (Plugins/<Name>/Content) this build carries. Their maps are cooked by name;
    every other folder must be always cooked (content loaded by path at run time: DefaultGame.ini),
    Maps not (a map's data and lightmaps come with the map, so --maps doesn't cook other maps')."""
    ini = (project / 'Config/DefaultGame.ini').read_text()
    plugins = []
    for content in sorted(project.glob('Plugins/*/Content')):
        name = content.parent.name
        for folder in sorted(d.name for d in content.iterdir() if d.is_dir() and d.name != 'Maps'):
            line = f'+DirectoriesToAlwaysCook=(Path="/{name}/{folder}")'
            if line not in ini:
                raise Fail(f'Config/DefaultGame.ini: add {line}')
        if not (public and name.startswith('MobileForces')):
            plugins.append(name)
    return plugins


def find_maps(project: Path, plugins: list[str], only: set[str]) -> list[str]:
    """Every built map (the cooker doesn't take maps from the always-cook folders)."""
    folders = [(project / 'Content/Maps', '/Game/Maps'), (project / 'Content/MF/Maps', '/Game/MF/Maps')]
    folders += [(project / 'Plugins' / p / 'Content/Maps', f'/{p}/Maps') for p in plugins]
    return [f'{mount}/{m.stem}' for folder, mount in folders for m in sorted(folder.glob('*.umap'))
            if not only or m.stem in only]


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


def split_downloads(paks: Path, chunk_lists: Path, out: Path, build_id: str, platform_name: str, team: bool = False) -> Path:
    """Move every pack but core (chunk 0) out of the staged build: one whose cooker list
    (pakchunkN.txt) names no classic package to <out>/Downloads/<platform>/<build id>/ with
    ChunkDownloader's BuildManifest-<platform>.txt (the game asks <server>/<platform>/<build id>/),
    one with classic content - converted from the player's own Mobile Forces, never on a public
    server - to <out>/Sideload/. With team, <out>/Private/<platform>/<build id>/ gets every pack,
    classic too, with its own manifest (hard links: no extra space where the file system has
    them): a team server's folder. No chunk ids are listed here: a new pack needs no change."""
    if not all((build_id, platform_name)):
        raise Fail('split_downloads: empty build id or platform')
    files = sorted(p for p in paks.glob('pakchunk*') if chunk_of(p.name) not in (None, 0))
    public = {}  # decided before moving anything, from what the cooker put in each chunk
    for n in sorted({chunk_of(p.name) for p in files}):
        listing = chunk_lists / f'pakchunk{n}.txt'
        if not listing.is_file():
            raise Fail(f'no cooker list for pakchunk{n}: {listing}')
        public[n] = CLASSIC_MARK not in listing.read_text(errors='replace').lower()
    upload, sideload = out / 'Downloads' / platform_name / build_id, out / 'Sideload'
    private = out / 'Private' / platform_name / build_id
    for d in (out / 'Downloads', sideload, out / 'Private'):
        shutil.rmtree(d, ignore_errors=True)
    for d in (upload, sideload, *([private] if team else [])):
        d.mkdir(parents=True)
    lines, all_lines = [], []
    for p in files:
        n = chunk_of(p.name)
        dest = (upload if public[n] else sideload) / p.name
        shutil.move(p, dest)
        line = manifest_line(dest, n)
        all_lines.append(line)
        if public[n]:
            lines.append(line)
        if team:
            try:
                os.link(dest, private / p.name)
            except OSError:  # a file system without hard links
                shutil.copy2(dest, private / p.name)
    write_manifest(upload, lines, build_id, platform_name)
    if team:
        write_manifest(private, all_lines, build_id, platform_name)
    return upload


# where a pack dropped in by hand is mounted at start: the install's own Content/Paks on the
# desktop; Saved/Paks in the app's storage on Android (the APK holds Content/Paks)
SIDELOAD_DIRS = {
    'Android': 'Android/data/com.fistodul.desktopforces/files/UnrealGame/DesktopForces/DesktopForces/Saved/Paks',
}


def classic_zip(out: Path, platform_name: str, build_id: str, label: str = '') -> Path | None:
    """<out>/DesktopForces-Classic-<label>.zip from <out>/Sideload/, laid out to be
    unzipped over the install (desktop: the folder with the game's executable; Android: the
    phone's storage root, from a computer over USB). For the team only, like Sideload/."""
    files = sorted((out / 'Sideload').glob('pakchunk*'))
    if not files:
        return None
    where = SIDELOAD_DIRS.get(platform_name, 'DesktopForces/Content/Paks')
    target = "the phone's storage (over USB from a computer)" if platform_name == 'Android' else 'the folder with the game (DesktopForces.exe / DesktopForces.sh)'
    readme = (f'Desktop Forces classic pack for {platform_name} build {build_id}.\n'
              f'Converted from the original Mobile Forces: for the team only, never share it publicly.\n\n'
              f'Unzip into {target}; the files land in {where}/.\n'
              f'It only works with game build {build_id} (Downloads page). Remove the files to drop the pack.\n')
    archive = out / f'DesktopForces-Classic-{label or platform_name + "-" + build_id}.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_STORED) as z:  # iostore containers are compressed already
        z.writestr('README-Classic.txt', readme)
        for p in files:
            z.write(p, f'{where}/{p.name}')
    return archive


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

    # editor modules (the cooker and the pack labels run them), as Tools/play.py builds them
    run([sys.executable, TOOLS / 'play.py', 'build'], a.dry_run)
    if a.compile_only:
        build = [batch / 'Build.bat'] if WINDOWS else [batch / 'Linux/Build.sh']
        run([*build, 'DesktopForces', UE_PLATFORMS.get(a.platform, a.platform), a.config, f'-Project={uproject}', '-WaitMutex', '-NoUBA'], a.dry_run)
        return

    suffix = '-public' if a.public else ''
    out = project / f'Saved/Packages/{a.platform}-{a.config}'
    out = out.with_name(out.name + suffix)
    env = android_env() if a.platform == 'Android' else os.environ.copy()

    # content packs -> their own paks (bGenerateChunks): label each pack root (cheap once they exist)
    editor_cmd = ue / 'Engine/Binaries' / host / ('UnrealEditor-Cmd.exe' if WINDOWS else 'UnrealEditor-Cmd')
    labels_log = project / 'Saved/Logs/PackLabels.log'
    labels_log.parent.mkdir(parents=True, exist_ok=True)
    run([editor_cmd, uproject, '-run=DFImport', '-steps=packs', '-unattended', '-nopause', '-nosplash', f'-abslog={labels_log}'],
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

    plugins = pack_plugins(project, a.public)
    maps = find_maps(project, plugins, set(filter(None, (a.maps or '').split(','))))

    uat = [batch / ('RunUAT.bat' if WINDOWS else 'RunUAT.sh'), 'BuildCookRun', f'-project={uproject}', '-noP4', '-utf8output',
           '-unattended', f'-platform={UE_PLATFORMS.get(a.platform, a.platform)}', f'-clientconfig={a.config}', '-pak', '-iostore', '-compressed']
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
    cook = ['-ubtargs=-NoUBA', '-build', '-cook', '-stage', '-prereqs'] + ([f'-map={"+".join(maps)}'] if maps else [])

    build_id_file = project / 'Content/Downloads/ContentBuildId.txt'
    uproject_text = uproject.read_text()
    version = game_version(project)  # before anything below changes the project
    game_ini = project / 'Config/DefaultGame.ini'
    game_ini_bytes = game_ini.read_bytes()  # restored byte for byte (line endings too)
    print(f'Packaging {a.platform} {a.config} {version}{" (public: no classic content)" if a.public else ""} -> {out} ({len(maps)} maps)')
    if a.dry_run:
        print('(dry run)')
    try:
        # the content build id (UDFDownloads): only in a --downloads build, only while it's made -
        # the paks on the server must come from the same cook as the game asking for them
        build_id = ''
        if not a.dry_run:
            build_id_file.parent.mkdir(parents=True, exist_ok=True)  # staged as UFS (DefaultGame.ini)
            build_id_file.unlink(missing_ok=True)
            shutil.rmtree(out, ignore_errors=True)  # paks from an earlier build would be mounted too
            game_ini.write_bytes(with_project_version(game_ini_bytes.decode('utf-8'), version).encode('utf-8'))
            if a.public:
                # the content plugins off in the project for this build only, back whatever happens
                data = json.loads(uproject_text)
                off = ('MobileForcesClassic', 'MobileForcesUnverified')
                data['Plugins'] = [p for p in data.get('Plugins', []) if p['Name'] not in off] + [{'Name': n, 'Enabled': False} for n in off]
                uproject.write_text(json.dumps(data, indent='\t') + '\n')
        if not a.downloads:
            run([*uat, *cook, '-archive', f'-archivedirectory={out}', *package_args], a.dry_run, env=env)
        else:
            # cook and stage, take the packs other than core out of the staged build, then package /
            # archive what's left
            stage_name, platform_name = STAGE_NAMES[a.platform]
            commit = subprocess.run(['git', '-C', str(project), 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True).stdout.strip()
            build_id = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d-%H%M%S') + '-' + (commit or 'local')
            stage = project / 'Saved/StagedBuilds' / stage_name
            if not a.dry_run:
                build_id_file.write_text(build_id)
                for obb in stage.parent.glob(f'{stage_name}*.obb'):  # else an earlier OBB, every pack in it, is reused
                    obb.unlink()
            run([*uat, *cook], a.dry_run, env=env)
            if not a.dry_run:
                split_downloads(stage / uproject.stem / 'Content/Paks', project / 'Saved/Cooked' / stage_name / uproject.stem / 'Metadata/ChunkManifest',
                                out.with_name(out.name + '-downloads'), build_id, platform_name, a.team)
                if a.team:
                    classic_zip(out.with_name(out.name + '-downloads'), platform_name, build_id, f'{a.platform}-{version}')
            run([*uat, '-skipcook', '-skipstage', '-skippak', '-archive', f'-archivedirectory={out}', *package_args], a.dry_run, env=env)
    finally:
        if not a.dry_run:
            build_id_file.unlink(missing_ok=True)
            uproject.write_text(uproject_text)
            game_ini.write_bytes(game_ini_bytes)

    if a.zip and not a.dry_run:
        # named by the version, the date releases go by: DesktopForces-<Platform>-<Config>[-public]-<version>
        archive = shutil.make_archive(str(out.parent / f'DesktopForces-{a.platform}-{a.config}{suffix}-{version}'), 'zip', out.parent, out.name)
        print(f'Zip: {archive}')
    print(f'Done: {out}, version {version}' + (f' (content build {build_id})' if build_id else ''))
    if a.downloads:
        print(f'  upload the contents of {out}-downloads/Downloads/ to the download server (DownloadServer in DefaultGame.ini)')
        print(f'  {out}-downloads/Sideload/: the classic pack - for your own devices only (Docs/Building.md)')
        if a.team:
            print(f'  {out}-downloads/Private/: every pack, classic too - for a team server only, never a public one')
            print(f'  {out}-downloads/DesktopForces-Classic-*.zip: the classic pack to unzip over a team member\'s install')


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('platform', nargs='?', choices=['Linux', 'Windows', 'Android'], default='Windows' if WINDOWS else 'Linux')
    ap.add_argument('config', nargs='?', choices=['Shipping', 'Development'], default='Shipping')
    ap.add_argument('--public', action='store_true', help='leave out the Classic and Unverified plugins')
    ap.add_argument('--downloads', action='store_true', help='core pack only in the build; the other packs for the server')
    ap.add_argument('--team', action='store_true', help='with --downloads: also a team server folder and a classic pack zip')
    ap.add_argument('--maps', help='comma-separated map names (default: every built map)')
    ap.add_argument('--zip', action='store_true', help='also pack the build into one .zip next to it')
    ap.add_argument('--compile-only', action='store_true', help='build the editor and game code, no cook')
    ap.add_argument('--clean', action='store_true', help="delete this host's Binaries / Intermediate/Build first")
    ap.add_argument('--dry-run', action='store_true', help='print the commands instead of running them')
    a = ap.parse_args(argv)
    if a.team and not a.downloads:
        ap.error('--team needs --downloads')
    try:
        package(a)
    except (Fail, OSError) as exc:
        print(f'package: {exc}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
