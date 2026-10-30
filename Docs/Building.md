# Building and tools

Use Python 3.11+ and Unreal Engine 5.8. The asset pipeline also needs Pillow, numpy,
ffmpeg, a Mobile Forces installation, and Wine on Linux. Windows C++ builds need
Visual Studio's C++ workload, a compatible Windows SDK and .NET Framework SDK;
`package.py` checks these before packaging.

## Build and play

`play.py` works on Linux and Windows. Replace `python3` with `py -3` on Windows if
needed. Run `python3 Tools/play.py --help` for the complete examples and mode list.

```bash
python3 Tools/setup.py --check
python3 Tools/play.py build -MaxParallelActions=4
python3 Tools/play.py mf-warehouse tdm 8
python3 Tools/play.py MF_TestRange dm 0
python3 Tools/play.py editor
python3 Tools/play.py --dry-run mf-warehouse dm 0 -RenderOffscreen -nosound
```

The launcher builds the Development editor target, including the game module. It
runs the game through UnrealEditor with `-game`; packaging produces a standalone
executable. Extra arguments are passed to Unreal. `DF_OPTS` adds URL options such
as `?ScoreLimit=10?Parachutes=0`.

Copy `Tools/mfport/mfport.example.toml` to the ignored `mfport.toml` and set your game
and engine paths. Then convert a map with `python3 Tools/mfport/mfport.py all mf-warehouse`.
See [Pipeline.md](Pipeline.md) for individual import steps.

## Engine and platform paths

Engine precedence: `UE_ROOT`, then `MFPORT_UE_ROOT`, then the current platform's
section in `mfport.toml`, then shared keys. Other keys accept `MFPORT_<KEY>`.
Relative paths are relative to `Tools/mfport`; moving a checkout can change what
its sibling-relative paths mean. Use absolute paths or `~` in platform sections:

```toml
work_dir = "work"
uproject = "../../DesktopForces.uproject"
[linux]
ue_root = '~/Linux_Unreal_Engine_5.8.4'
game_dir = '~/Games/MobileForces'

[windows]
game_dir = 'C:\Program Files (x86)\Steam\steamapps\common\Mobile Forces'
ue_root = 'C:\Program Files\Epic Games\UE_5.8'
```

Windows paths need single-quoted TOML strings or forward slashes. Setup escapes
its generated paths automatically. `ue_root = "auto"` selects the newest installed
patch matching the project's major/minor using `Engine/Build/Build.version`.
Explicit paths stay pinned. Linux searches home, the project parent and `/opt`;
Windows checks Epic's registry/installation list and usual install directories.
`python3 Tools/play.py --engine-root` prints the result.

5.8.3 and 5.8.4 use `EngineAssociation = "5.8"`; rebuild C++ modules after switching
patches. There is no need to change the project association to a patch number.

## Package

`Tools/package.py` packages on Linux and Windows (`py -3` instead of `python3` on Windows);
`--help` lists every option:

```bash
python3 Tools/package.py Linux Development
python3 Tools/package.py --public
python3 Tools/package.py Windows Development --zip    # on Windows
python3 Tools/package.py Windows Development --compile-only
python3 Tools/package.py --dry-run Windows
```

It builds the editor modules (as `play.py build`), labels the packs, runs Unreal's BuildCookRun
with the map list, compressed paks and iostore, and on Windows first checks the toolchain. The
engine is the launcher's (`UE_ROOT`, `mfport.toml`). Outputs, for every platform:

| Folder | What |
|---|---|
| `Saved/Packages/<Platform>-<Config>[-public]/` | the game build (Shipping: release; Development: console and logs) |
| `Saved/Packages/<…>-downloads/Downloads/` | `--downloads`: the packs for the public server (upload its contents) |
| `Saved/Packages/<…>-downloads/Sideload/` | `--downloads`: the classic pack, for the team only |

Each build carries its version, its commit's date (`20261030`, as releases are named; editor runs
say `dev`): `ProjectVersion`, stamped into `DefaultGame.ini` while packaging. It is the network
version: players join only servers of the same version, whatever their platform, so one day's
builds play together and other days' are refused instead of desyncing (two builds of one day with
different code would still be let in: rebuild every platform after a fix that changes networking).
`--zip` names the archive after it; Credits shows it.

`Build/<Platform>/` holds Unreal's own build data (pak file order, chunk layout, generated Android
sources), not builds.

Every build splits its content into one pak per pack (`Content/Paks/pakchunkN-<Platform>`):

| Chunk | Pack | Content | Size (3-map Linux build) |
|---|---|---|---|
| 0 | core | the game: `/Game/MF`, engine content, anything unlabelled | 51 MB |
| 1 | community | `/Game/Maps`, `/Game/MF/Community`, `/Game/MF/Replacements` (agreed authors) | 225 MB |
| 2 | unverified | `/MobileForcesUnverified` (custom maps and mods not cleared yet) | 5 MB |
| 3 | classic | `/MobileForcesClassic` (from your MF install: never share it) | 442 MB |

A pack is just its three files (`.pak`, `.ucas`, `.utoc`). Drop them into `Content/Paks` to add the
pack, or remove them to drop it; the menu and votes list only the maps whose pak is there. The
packagers write a `PrimaryAssetLabel` (`PAL_<Pack>`) into each pack root before cooking
(`UnrealEditor-Cmd <uproject> -run=DFImport -steps=packs`); `/Game/MF/Replacements` (replacement
art the community maps use) is labelled community. Every label needs its own name (`PAL_Community`,
`PAL_CommunityArt`, `PAL_Replacements` ...): a label's primary asset id is its name, a second one of
the same name is ignored and its folder falls into chunk 0; the packagers stop on that warning. A label tags its own folder only, and maps
don't claim what they use (the `Map` asset type is non-recursive in `DefaultGame.ini`), so an
asset stays in its pack's pak even when another pack uses it: community maps that borrow classic
textures show them only while the classic pak is installed. (Until 2026-10-29 maps were recursive
and the community pak carried copies of the ~300 classic textures and sounds its maps borrow.)
`python3 Tools/mfport/mfport.py packs` shows what is in each pack: every converted map with its
pack, and per platform what the last cook put in each pak (files per folder, the maps), read from
the cooker's lists `Saved/Cooked/<Platform>/DesktopForces/Metadata/ChunkManifest/pakchunkN.txt`.

### Downloadable packs

`--downloads` makes a build that carries the core pack (chunk 0) only and
fetches the others in game (menu > Downloads; UE's ChunkDownloader plugin, `Core/DFDownloads`):

```bash
python3 Tools/package.py Android --downloads
python3 Tools/package.py Linux --downloads
```

It cooks and stages as usual, then moves `pakchunk1`/`2` (community,
unverified) out of the staged build into `<output>-downloads/Downloads/<Platform>/<content build
id>/` with a `BuildManifest-<Platform>.txt` (file, size, SHA1, chunk) and `pakchunk3` (classic)
into `<output>-downloads/Sideload/`, and the packager archives what's left. Copy the contents of
`Downloads/` to the server, `https://mf.nofisto.com/download/` (`DownloadServer` in
`DefaultGame.ini`; players can set another in Options > Game): the game asks
`<server>/<Platform>/<content build id>/`, e.g. `download/Android/20261029-125729-94671c9/`.
Each build has its own content build id (`<UTC time>-<git commit>`, in
`Content/Downloads/ContentBuildId.txt` while packaging): a game only downloads paks cooked with it,
so keep the folders of builds players still have. A player can also take a build's paks from there
by hand and drop them into `Saved/Paks/`.

Cooking is deterministic: a pack whose content didn't change comes out byte for byte the same in
the next build, so players keep it across updates (ChunkDownloader keeps a file whose SHA1 is
unchanged) and the server can hardlink it between build folders
(`rsync --link-dest=<previous build> ...`). Platforms can't share paks: each carries its
platform's cooked data and shaders (Android: ASTC textures, mobile shaders).

The packager refuses to put a chunk into `Downloads/` whose cooker list names a classic package.
**Classic content never goes on a public server.** For your own devices, copy the `Sideload/`
files into the game's `Saved/Paks/` (mounted at start; on Android
`/sdcard/Android/data/com.fistodul.desktopforces/files/UnrealGame/DesktopForces/DesktopForces/Saved/Paks/`).

In game, downloads go to the app's own storage (`PersistentDownloadDir/PakCache`), one file at a
time, each checked against its SHA1; a stopped download goes on from the last whole file
(containers are split into 64 MB parts, `MaxIoStorePartitionSizeMB`, because ChunkDownloader holds
the file it is downloading in memory). A finished pack mounts at once and its maps join the map
list; installed packs mount at every start. Delete unmounts the pack and removes its files. Test
against a LAN server:

```bash
cd Saved/Packages/Android-Development-downloads/Downloads && python3 -m http.server 8000
# the game pointed at it: Options > Game > Download server, or (UECommandLine.txt on a phone)
... -ini:Game:[/Script/DesktopForces.DFGameSettings]:DownloadServer=http://<this PC's LAN IP>:8000
```

Menu commands for scripted tests: `DFMenuPage 5`, `DFPacks`, `DFPackDownload N`,
`DFPackCancel N`, `DFPackDelete N` ([Commands](Commands.md)).

`--public` disables the Classic and Unverified content plugins for that
build, restoring the project file afterward. Maps still borrowing their textures
will need replacements (`mfport.py packs`). Builds containing original game content
are for the team; do not publish that content with the public source.

## Android

Needs the SDK packages the engine names in `Engine/Config/Android/Android_SDK.json`
(5.8: `platforms;android-36`, `build-tools;36.0.0`, `cmake;3.22.1`, `ndk;27.2.12479018`,
plus `platform-tools`) and a JDK 17+. Android Studio isn't required. Google's command-line
tools are enough:

```bash
mkdir -p ~/Android/Sdk/cmdline-tools && cd ~/Android/Sdk/cmdline-tools
# unzip commandlinetools-linux-*_latest.zip from developer.android.com here, rename to "latest"
yes | latest/bin/sdkmanager --sdk_root=$HOME/Android/Sdk --licenses
latest/bin/sdkmanager --sdk_root=$HOME/Android/Sdk "platform-tools" "platforms;android-36" \
  "build-tools;36.0.0" "cmake;3.22.1" "ndk;27.2.12479018"
```

`python3 Tools/package.py Android` builds an arm64 APK with ASTC textures and every map (Shipping;
add `Development` for the console and logs). With `--downloads` the game data (the core pack,
~50 MB) is inside the APK: one file for players to install. Otherwise it goes in a separate
`main.<version>.com.fistodul.desktopforces.obb` next to `DesktopForces-arm64.apk` in
`Saved/Packages/Android-<Config>/` (`bPackageDataInsideApk=False`; past 4 GB the rest spills into
patch / overflow OBBs). `--maps MF_TestRange,mf-warehouse` limits the map list: the pack plugins'
folders other than `Maps` are still always cooked (`DirectoriesToAlwaysCook`, content loaded by
path at run time), the other maps' data and lightmaps not. The packagers stop when a pack folder
has no `DirectoriesToAlwaysCook` line.
`ANDROID_HOME`, `NDKROOT` and `JAVA_HOME` override `~/Android/Sdk`, its newest NDK, and the
JDK of `javac`. Install with the generated `Install_DesktopForces-arm64.sh` (APK + OBBs; it
uninstalls first, wiping `UECommandLine.txt`), or by hand: `adb install -r` the APK and
`adb push` each `.obb` to `/sdcard/Android/obb/com.fistodul.desktopforces/`.

Testing on a phone (adb is `~/Android/Sdk/platform-tools/adb`; enable Wireless debugging on the
phone, and "Stay awake" so it doesn't lock between runs):

```bash
adb pair <ip>:<pairing port> <code>      # once; then the phone shows up by itself:
adb mdns services && adb devices
Saved/Packages/Android-Development/Install_DesktopForces-arm64.sh   # APK + OBB
# start straight into a map with scripted commands (Development builds read this file):
echo '../../../DesktopForces/DesktopForces.uproject /MobileForcesClassic/Maps/mf-warehouse?game=dm?Bots=6 -DFExec="@6:DFLoadout|@7:stat unit"' > UECommandLine.txt
adb push UECommandLine.txt /sdcard/Android/data/com.fistodul.desktopforces/files/UnrealGame/DesktopForces/
adb shell monkey -p com.fistodul.desktopforces -c android.intent.category.LAUNCHER 1
adb exec-out screencap -p > shot.png    # look
adb shell input tap X Y                 # touch (swipe X1 Y1 X2 Y2 ms holds a stick)
adb shell am force-stop com.fistodul.desktopforces   # always, after a test
```

Phones default to 30 FPS with VSync and a 75 % render scale
(`Config/Android/AndroidGameUserSettings.ini`). Uncapped, the game starved the phone until
System UI stopped responding. `adb shell pm clear com.fistodul.desktopforces` resets saved
settings.

Touch controls are UE's virtual joysticks (left moves, right looks) plus `SDFTouchControls`
buttons. They appear on touch devices; test them on a desktop with
`-ini:Input:[/Script/Engine.InputSettings]:bAlwaysShowTouchInterface=True`.

## Shared checkout or separate copies

For dual boot, put the checkout on a drive both systems can access and optionally
make a Linux symlink to it with `ln -s`. Moving folders and creating the link are
ordinary filesystem operations; no project-specific migration script is needed.
Both hosts use separate platform build directories. Keep Windows paths short.

To maintain a separate copy, use explicit local paths:

```bash
bash Tools/sync_files.sh /path/to/DesktopForces /path/to/copy --dry-run
bash Tools/sync_files.sh /path/to/DesktopForces /path/to/copy
bash Tools/sync_files.sh /path/to/DesktopForces /path/to/copy --delete
```

Requires Bash, rsync and realpath (Linux/WSL/Git Bash with these installed). Build
outputs, Git data, caches and local `mfport.toml` are excluded. `--delete` is opt-in;
excluded destination files remain protected. Identical paths do nothing; nested
source/destination paths are rejected.

## Download Windows Unreal from Linux

Install [Legendary](https://github.com/legendary-gl/legendary) in your chosen environment,
put `legendary` on PATH and run `legendary auth` once. The helper uses its normal CLI:

```bash
python3 Tools/download_ue_windows.py --target-dir '/mounted/drive/Epic Games/UE_5.8' --dry-run
python3 Tools/download_ue_windows.py --target-dir '/mounted/drive/Epic Games/UE_5.8'
```

`--include-symbols` adds debug symbols. Dry run prints commands without network or
login. The helper populates UE metadata and calls `legendary install`; it does not
inject Python package paths or edit Epic's `LauncherInstalled.dat`. Use `UE_ROOT`
or `-UePath` for a nonstandard install. Epic Games Launcher on Windows remains an
alternative.

## Public GitHub sync

```bash
bash Tools/sync_github.sh                # preview committed changes using cached github/main
bash Tools/sync_github.sh --push         # fetch, prepare every commit, then push
```

Individual source commits retain their authors, dates and messages. Mixed commits
are filtered using `.gitignore.github`; original-game content and excluded generated
assets are removed from each exported tree. A source-SHA trailer and patch IDs recognize
new exports and older cherry-picks. Content-only and already-applied commits are skipped.

The script uses temporary Git indexes instead of switching branches or checking out
thousands of assets. It leaves the working files, real index and local branches alone.
Preview creates unreachable Git objects for inspection, but changes no refs and does
not push. Uncommitted edits are never included. Conflicts or merge commits stop the
operation; merges require manual handling rather than silently dropping changes.
`--push` uses a normal fast-forward push, with no force option.

## Formatting and tests

clang-format reads the project's `.clang-format` automatically. It does not recursively
find files itself; Bash can supply the source files in one command:

```bash
find Source -type f \( -name '*.h' -o -name '*.cpp' \) -exec clang-format -i {} +
clang-format --dry-run --Werror Source/DesktopForces/UI/DFMenu.cpp
python3 -m unittest discover -s Tools/tests -v
```

`Tools/tests/test_tools.py` checks path/configuration and launch regressions;
`test_sync.py` tests real Git and rsync behavior in disposable directories. Neither
runs during normal play or packaging. The native `DesktopForces.Gameplay.Options`
automation test runs inside a standalone/listen game and covers settings and gameplay
state transitions.
