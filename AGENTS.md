# Repository guidance

## Project and layout

Desktop Forces is a team shooter for low-end PCs, built with Unreal Engine 5.8.
Run commands from the directory containing `DesktopForces.uproject`.

- `Source/DesktopForces/`: runtime C++ module. Framework code lives in `Core/`,
  `Player/`, `AI/`, `Loadout/`, `UI/`, and `World/`; modes, weapons, and vehicles
  have their own folders under `Game/`, `Weapons/`, and `Vehicles/`.
- `Source/DesktopForcesEditor/`: editor-only importer (`-run=DFImport`), asset
  creation, and level building. Keep editor dependencies out of the runtime module.
- `Tools/mfport/`: Python conversion pipeline; `mflib/` implements package reading
  and conversion. Keep Mobile Forces format knowledge here and the C++ importer generic.
- `Config/`: project defaults, game settings, input, scalability, and class aliases.
- `SourceArt/` and `Tools/artgen/`: original art and procedural asset generation.
- `Docs/`: start with `Architecture.md`; consult `Gameplay.md`, `Performance.md`,
  `Pipeline.md`, `Modding.md`, `Building.md`, and `Commands.md` for the affected area.

## Build and run

Use the maintained scripts. Generated IDE files and the local Makefile can still
refer to the former `MobileForces` project name.

```bash
python3 Tools/setup.py --check                  # prerequisites
python3 Tools/play.py build                            # Linux editor + game modules
python3 Tools/play.py editor                           # open Unreal Editor
python3 Tools/play.py MF_TestRange dm 0                 # built-in test map
python3 Tools/play.py mf-warehouse tdm 8                # converted map with bots
clang-format --dry-run --Werror Source/path/File.cpp    # check changed C++ files
clang-format -i Source/path/File.cpp            # format changed C++ files
```

`Tools/play.py` is the shared Linux/Windows launcher.
It uses `UE_ROOT`, `MFPORT_UE_ROOT`, or the host section/shared `ue_root` in
`Tools/mfport/mfport.toml`; `auto` selects the newest installed compatible patch.
The Python pipeline accepts `MFPORT_<KEY>` overrides, including `MFPORT_UE_ROOT`.
Copy `Tools/mfport/mfport.example.toml` for local configuration when needed;
keep machine-specific paths in that ignored config or the environment. This
checkout may be shared with Windows through a symlink, so verify resolved paths.

Windows compile command (PowerShell):

```powershell
py -3 Tools/package.py Windows Development --compile-only
```

Pipeline prerequisites are Python 3.11+, Pillow, numpy, ffmpeg, and a local Mobile
Forces installation; Linux conversion also uses Wine. A focused conversion is
`python3 Tools/mfport/mfport.py all mf-warehouse`. Prefer the specific conversion
and import steps described in `Docs/Pipeline.md` when only part of the output changes.
For packaging, use `python3 Tools/package.py Linux Development` (Windows: `py -3 Tools/package.py`);
see `Docs/Building.md` for public builds and platform requirements.

## Implementation conventions

- Follow `.clang-format` (written for clang-format 18): tabs, same-line braces,
  no braces around single statements, and no forced line wrapping. Preserve
  `clang-format off/on` regions around Slate declarations. Format touched files.
- Framework classes use `DF` (`ADFCharacter`, `UDFGameSettings`), namespace `DF::`,
  and log category `LogDF`. Concrete gameplay classes use plain names such as
  `APistol`, `ABuggy`, and `ACaptureTheFlag`. Follow adjacent code and UE prefixes.
- Avoid local or parameter names that shadow inherited members, such as `Owner`,
  `Instigator`, or `Mesh`; these can fail Windows/MSVC builds.
- Prefer shallow hierarchies, UE facilities, and config/property-driven variation.
  Teams, bots, and loadout entries use collections rather than fixed limits.
- Preserve server authority: gameplay changes, damage, and loadout selection go
  through server logic/RPCs; keep prediction and replication consistent.
- Preserve original gameplay values and comments identifying their source. Check
  `Docs/Gameplay.md` before tuning; the pipeline uses `UU_TO_CM = 2.25` for MF units.
- Decompiled `.uc` defaults omit any value (or struct field) equal to the parent class's,
  so a missing field is inherited, not zero. Resolve the parent chain before porting
  ([Pipeline](Docs/Pipeline.md#reading-a-mods-code)).
- Follow `Docs/Performance.md`: forward rendering, baked lighting and navigation,
  small textures, and timers instead of unnecessary actor ticks. Avoid per-frame
  world scans and introducing expensive rendering features.

## Assets and version control

Original game content is converted locally into `Plugins/MobileForcesClassic`.
Runtime code must tolerate its absence; use existing helpers in `Core/DFContent.h`.
Never commit original game data, `Plugins/*/Content/`, or `Tools/mfport/work/`.
Keep build products and caches (`Binaries/`, `Intermediate/`, `Saved/`,
`DerivedDataCache/`, `Build/`) out of source changes.

Content tracking differs between the Azure content branch and the public GitHub
branch. Check the current branch, `.gitignore`, `.gitignore.github`, and
`Tools/mfport/packs.toml` before staging assets; some older docs describe only the
source-only checkout. Author-cleared community assets may be tracked on Azure.
Treat `.uasset` and `.umap` files as binary and regenerate/edit them with Unreal
or the pipeline. Preserve `.gitattributes` line-ending rules for both platforms.
`Tools/sync_github.sh` previews filtered commits through temporary Git indexes.
Only `--push` publishes; it never switches the working checkout.

## Validation

`python3 -m unittest discover -s Tools/tests -v` checks configuration and sync tools.
Native gameplay regression checks live in `Source/DesktopForces/Tests/` (ClientContext: run them
in `-game` on a map, rendering, e.g. `UnrealEditor <uproject> /Game/MF/Maps/MF_TestRange -game
-RenderOffscreen -ExecCmds="Automation RunTests DesktopForces" -unattended -TestExit="Automation
Test Queue Empty"`; `-nullrhi` fails the effect checks). For C++ changes,
check formatting, build the editor target, and exercise the affected behavior.
For gameplay/network changes, include the relevant mode and server/client behavior.
For Python changes, check syntax and run the affected pipeline step on a small input.
Documentation-only changes do not need an Unreal build.

A bounded graphical smoke run is:

```bash
python3 Tools/play.py MF_TestRange dm 0 -windowed -log -DFAutoShot=6 -DFShotName=smoke -DFQuitAfter=8
```

`-DFExec="@3:command|@5:other-command"` schedules console actions; see
`Docs/Commands.md` and `Core/DFGameInstance.cpp`. Inspect `Saved/Logs/` and
`Saved/Screenshots/` for results. A successful launch alone does not verify gameplay.
Report what was checked and any checks blocked by missing engine, content, or display.

## Lifecycle and local-test pitfalls

- Attached pooled particle components can belong to WorldSettings, not the actor they follow.
  Store handles to looping effects and stop emission on explosion **and** EndPlay on every
  peer. A successful launch screenshot does not test cleanup after destruction or timeout.
- Collision changes can synchronously invoke overlap/damage/death callbacks. Treat vehicle
  exit as a transaction: disable collision, detach and place the pawn, restore possession,
  then restore movement/collision only if the pawn is still alive. Test the whole lifecycle,
  including death and respawn; never resume movement on a dead pawn.
- Keep seated occupants queryable for weapon hits. Disable physical responses selectively;
  disabling all actor collision also makes exposed drivers invulnerable to traces.
- Check original MF door/seat and near-driver metadata before changing seat selection.
  Front-seat driver preference and unredirected closest-seat entry are separate behaviors.
- Bot input bookkeeping must follow StopFire, reload, and weapon changes. Exercise a second
  magazine, not just initial firing, when checking combat behavior.
- Use absolute artifact paths outside the project. This checkout resolves through a Windows
  drive symlink, so a shell's `../tests` can resolve somewhere different from its logical path.
- Coordinate a single Unreal execution lane across agents: builds/UAT share locks, and starting
  another editor or commandlet can restart Zen during a cook. An idle process list does not
  revoke an explicit request to keep Unreal work paused; wait for the lane owner to release it.
- When `-TestExit="Automation Test Queue Empty"` forces exit, Linux can return code 1 even
  after every test succeeds. Verify all expected `Test Completed. Result={Success}` entries
  and the explicit TestExit reason; do not generally treat exit code 1 as success.
- No weekly quota meter is exposed through the current tools. Do not claim a precise remaining
  percentage. Keep a progress checkpoint and report any actual quota/approval rejection.

- Sleeping rigid bodies still need gameplay recovery checks when overturned. An early return
  for settled vehicles must not bypass the righting timer or torque.
- Let newly registered physics components enter the scene before testing collision. An immediate
  same-frame query can miss valid geometry; defer fixture queries at least one physics tick.

## Working in focused sessions

Keep one conversation per problem. `Docs/Roadmap.md` "Open threads" lists the current
hand-off items with their plans. Several agents may share this working tree: check
`git status` first, don't commit staged work that isn't yours (`git commit -- <your paths>`),
and record new findings in the relevant doc rather than in chat.
