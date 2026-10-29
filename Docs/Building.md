# Building and tools

Use Python 3.11+ and Unreal Engine 5.8. The asset pipeline also needs Pillow, numpy,
ffmpeg, a Mobile Forces installation, and Wine on Linux. Windows C++ builds need
Visual Studio's C++ workload, a compatible Windows SDK and .NET Framework SDK;
`package.ps1` checks these before packaging.

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

The scripts have the same basename and platform-appropriate extensions:

```bash
bash Tools/package.sh Linux Development
bash Tools/package.sh --public
```

```powershell
powershell -ExecutionPolicy Bypass -File Tools/package.ps1 -Config Development -Zip
powershell -ExecutionPolicy Bypass -File Tools/package.ps1 -Public
powershell -ExecutionPolicy Bypass -File Tools/package.ps1 -Package:$false
pwsh Tools/package.ps1 -DryRun
```

Both run Unreal's BuildCookRun and include the map list, compressed paks and iostore.
The Windows script also checks the Windows toolchain, builds editor modules and
can produce a ZIP. It uses the launcher's engine discovery unless `-UePath` is given.
Outputs: `Saved/Packages/<platform>-<config>/` on Linux; `Build/Windows/` on Windows.

Every build splits its content into one pak per pack (`Content/Paks/pakchunkN-<Platform>`):

| Chunk | Pack | Content | Size (3-map Linux build) |
|---|---|---|---|
| 0 | core | the game: `/Game/MF`, engine content, anything unlabelled | 51 MB |
| 1 | community | `/Game/Maps`, `/Game/MF/Community` (agreed authors) | 225 MB |
| 2 | unverified | `/MobileForcesUnverified` | 5 MB |
| 3 | classic | `/MobileForcesClassic` (from your MF install: never share it) | 442 MB |

A pack is just its three files (`.pak`, `.ucas`, `.utoc`). Drop them into `Content/Paks` to add the
pack, or remove them to drop it; the menu and votes list only the maps whose pak is there. The
packagers write a `PrimaryAssetLabel` (`PAL_<Pack>`) into each pack root before cooking
(`UnrealEditor-Cmd <uproject> -run=DFImport -steps=packs`). A label tags its own folder only,
so an asset stays in its pack's pak even when another pack uses it: community maps that borrow
classic textures show them only while the classic pak is installed. The packagers empty the
output folder first, so paks from an earlier build aren't mounted with the new ones.

`--public` / `-Public` disables the Classic and Unverified content plugins for that
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

`Tools/package.sh Android` builds an arm64 APK with ASTC textures and every map (Shipping;
add `Development` for the console and logs). The game data goes in a separate
`main.<version>.com.fistodul.desktopforces.obb` next to `DesktopForces-arm64.apk` in
`Saved/Packages/Android-<Config>/` (`bPackageDataInsideApk=False`; past 4 GB the rest spills into
patch / overflow OBBs). `--maps MF_TestRange,mf-warehouse` limits the map list, but every
content folder in `DirectoriesToAlwaysCook` is still cooked, the whole Classic plugin included.
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
