# Performance rules

Target: phones and potato PCs (reference dev machine: 6 GB VRAM laptop GPU). The desktop
build uses mobile-class settings too.

## Rendering (Config/DefaultEngine.ini)
- **Forward shading**, MSAA (scaled by AA quality: off / 2× / 4×). No TAA/TSR.
- **No post-processing** by default: bloom, AO, auto exposure, motion blur, lens flare, DOF
  off at Low/Medium/High (`DefaultScalability.ini`). Epic post quality adds a little bloom.
- **Render scale** (Video > Advanced Graphics, `RenderScale X`): the 3D view at 25 % - 200 % of the window
  (`r.ScreenPercentage`), HUD and menus stay at full resolution. 100 % is the default and renders at the output resolution; 50 % is for very weak GPUs, above 100 % supersamples.
- **Graphics headroom**: players can raise scalability (`scalability 0-3` or options):
  higher levels add MSAA, anisotropic filtering (0/2/4/8×), view distance and Epic bloom.
  The defaults stay potato.
- **Static lighting only**: `r.AllowStaticLighting=1`, Lumen/MegaLights off, no virtual
  shadow maps, no ray tracing, no distance fields, no sky atmosphere / volumetric fog.
  Every light the pipeline places is Static; maps are baked with Lightmass
  (`mfport.py light`), plus precomputed visibility for occlusion.
- Mobile: forward path, `r.MobileHDR=0` (LDR → no post-process pass at all), no local lights
  at runtime (`r.Mobile.Forward.EnableLocalLights=0`).
- Nanite, virtual textures, Substrate off. Shader targets SM5 (DX11/Vulkan) + ES3.1 only.
- Texture streaming off (textures are ≤ 512 px, `NeverStream`). Texture quality Low samples
  one mip down (`r.MipMapLODBias=1`: 512 px textures draw as 256); `max_texture_size = 256` in
  `mfport.toml` imports everything at 256 for the smallest memory. A converted MF map's
  textures are ~50 MB at 512 px, so 4 GB VRAM cards have lots of room.
- Lighting is all baked: two Lightmass bounces, soft light radii, sky light through sky
  windows - none of it costs anything at runtime. The only live lighting term is one
  multiply-add in the lit materials (`BaseColor × MPC_MF.Ambient`, the map's ambient floor).

## Content
- Textures imported at original resolution up to 512 px (`max_texture_size`); Android/iOS
  device profiles cook World/UI textures down to 256 px. No alpha channel unless masked
  (half the memory with BC1/ETC2).
- Navmesh is baked into each map by the importer (no runtime generation).
- Level geometry is merged into ~2048 uu (46 m) grid chunks, one draw per material per chunk;
  collision lives in separate invisible meshes so render meshes carry no physics.
- Lightmap resolution follows surface area (16 uu texels), capped at 1024.
- Planned: texture arrays / trim sheets per map to collapse materials (1–3 draws per chunk),
  HLOD/level streaming by MF zone for the big outdoor maps.

## Code
- Actors don't tick unless they must. `ADFCharacter` ticks only when locally controlled
  (camera FOV + recoil); weapons, projectiles, crates, map actors never tick — timers only.
- Canvas HUD (no UMG widget tree to tick/layout); menus are plain Slate, only while open.
- Skeletal meshes: `OnlyTickPoseWhenRendered`.
- Net: 30 Hz server tick, weapons 10 Hz net update, characters 30 Hz.
- Prefer `TActorIterator` only at events (spawn, loadout), never per frame.

## Plugins
~80 default plugins disabled in `DesktopForces.uproject` (media players, ArchVis, PCG,
MetaHuman, Paper2D, cloth, ControlRig extras, source control providers …). Only disable a
plugin if nothing enabled depends on it — the list was computed from the `.uplugin`
dependency graph.

## Editor
`Config/DefaultEditorSettings.ini` starts the editor viewport at scalability Low. UBT:
if builds hang with `clang++` stuck in "D" state, kill leftover clang processes from an
aborted build (they wait on a dead UBA session); `-NoUBA` disables UBA detours.

## Player graphics controls

New profiles use 120 FPS, VSync off, native borderless output and actual Low quality.
FPS, VSync and render scale are in Advanced Graphics. VSync and the FPS cap are
independent. Render scale uses 25% steps from 25% to 200%; presets leave it alone.
`RenderScale` mirrors its requested percentage into `sg.ResolutionQuality` when applied.
The extra screen-percentage assignment permits supersampling above Unreal's standard
100% scalability ceiling. FPS, VSync and window changes use UGameUserSettings itself.

On Linux, UE treats Fullscreen and Borderless identically, so the menu offers just
Borderless and Window. Borderless uses desktop resolution; Window supports size changes.
Platforms supporting exclusive fullscreen retain that third option. There is no hidden
render-scale multiplier tied to a stale borderless resolution. Low uses no MSAA, Medium
2×, High/Epic 4×; higher texture presets reset Low's mip bias.

`UDFUserSettings` extends Unreal's `UGameUserSettings` so the same saved profile also
holds crosshair, audio, key-binding and console preferences. It does not replace Unreal's
resolution, FPS or VSync implementation. `SetToDefaults()` calls the engine implementation
first, then establishes the project's defaults; saved preferences load afterward. The
constructor calls it explicitly because a base constructor cannot dispatch a derived
override. The derived INI section stores inherited fields too, so duplicating those fields
under the base `GameUserSettings` section is unnecessary.

Full scorch marks use a surface-conforming mesh (289 samples, at most 512 triangles per mark)
projected once at impact. There are at most 24 active full marks. This adds bounded CPU work
at explosions and a draw per mark; Simple retains instanced flat quads. Both work in forward
rendering with DBuffer disabled, including unlit/emissive world materials.

Defaults uses a fresh settings object followed by `SetToDefaults()`, rather than the config-
loaded class default object. It preserves the active tab and Advanced expansion. Identity,
window mode, resolution and graphics preset remain unchanged. Pending settings take effect
with Apply; the console checkbox follows the live viewport unless explicitly edited.

Vehicle damage smoke and missile trails retain their bounded emission rates on Low effects
quality, so damage and incoming-projectile cues remain visible. A heavily damaged vehicle
holds at most about 32 smoke sprites; a rocket or Predator holds about 38 trail sprites.
Other effects still use Unreal's global particle scalability. Smoke parameters update four
times per second from replicated health, with no smoke emitter on an undamaged vehicle.


## Template and plugin audit (2026-10-29)

All 55 queued maps are converted, imported, and baked. Their remaining work is playtesting,
not another bulk import. The active roadmap still includes borrowed-classic replacement art,
vehicle ride-along/navigation tests, the flat warehouse crate, and Android HUD safe zones.

The importer now compares source-file MD5 and texture settings before reimporting/saving
(`DFImporter.cpp`, commit `e296d89`). Equal source hashes explain redundant reserialization;
they do not prove that differently named assets or material instances are interchangeable.
Do not delete packages by hash without checking references, settings, and pack ownership.

### Templates and Starter Content

| Content | Recommendation for this project |
|---|---|
| Starter Content | Use selected simple props, surface textures, and suitable sounds as replacement-art candidates. Test in a scratch project and migrate only selected assets and dependencies. It is not a complete replacement for MF-specific signs, team art, or weapon sounds. |
| Third Person template | Keep as a reference or isolated animation/ragdoll prototype. Both it and DFCharacter already derive from ACharacter; the game already uses CharacterMovement and Enhanced Input. Its mannequin animations do not directly fit the imported MF skeletons. Importing its pawn/game mode/input assets would add parallel systems. |
| Vehicle template | Use an isolated handling benchmark if needed. It uses AWheeledVehiclePawn/UChaosWheeledVehicleMovementComponent; DF uses a custom rigid body with raycast suspension, MF handling, seats, wheel damage, and replication. Migration is a substantial gameplay rewrite, with no demonstrated performance advantage. |

Unreferenced source assets on disk do not inherently increase GPU work. Assets that are used,
cooked, or retained by broad cook rules can add rendering work, shader variants, or download
size. Keep replacement materials on the existing inexpensive forward-rendered material path;
avoid importing a whole material/effect library to replace one surface.

### Plugin candidates

The project already explicitly disables 80 plugins. The enabled list in the project file is
not the complete effective set: engine defaults and dependency chains enable others.

- **StateTree + GameplayStateTree:** first removal candidate. The source references found are
  the two Build.cs dependencies; bots use their own timed Think logic. Remove those module
  dependencies together with the plugin entries only after an asset-reference check, then
  rebuild/cook and run the bot/lifecycle tests. Expected gains are build/load footprint,
  not a demonstrated reduction in steady-state GPU time.
- **Keep EnhancedInput, ProceduralMeshComponent, and ChunkDownloader:** live input,
  surface-conforming scorch marks, and downloadable packs use them.
- **Keep the import toolchain:** skeletal imports use Interchange/glTF; the editor module
  explicitly links EditorScriptingUtilities. Modeling Tools is already Editor-only and can
  help inspect/repair the reported geometry. Disabling it would not remove a shipped viewport
  rendering cost. Python/editor utilities should be judged by actual tooling use.
- **Do not independently disable Niagara, ControlRig, or IK Rig just because DF source lacks
  direct calls.** In the installed engine, Interchange requires HairStrands/GeometryCache,
  which require Niagara. IK Rig requires ControlRig and FullBodyIK. Test an editor-versus-game
  dependency split in an isolated configuration before pruning these chains. A plugin name
  in a build receipt is not evidence that all its editor modules run in a shipped game.
- **Animation Budget Allocator / Animation Sharing:** candidates only if profiling many bots
  identifies animation CPU cost. Budget allocation needs budgeted mesh components and can
  reduce update rates; sharing needs compatible animation-state setup. Neither is a free
  checkbox improvement. DF already enables update-rate optimization and skips offscreen poses.

### Performance work with the best evidence

1. Profile packaged CPU, draw-thread, and GPU time before changing features. The roadmap's
   Pixel 9a result is about 15 ms game, 12 ms draw, and 6.5 ms GPU at a 30 FPS cap. Those are
   parallel pipeline timings, not numbers to add. A 60 FPS target has only 16.7 ms per frame,
   so game-thread headroom is much smaller than the GPU result alone suggests.
2. Test ISM/HISM grouping for repeated static props with matching meshes/materials. The map
   builder currently creates separate StaticMeshActors. Preserve collision, lightmaps,
   material variation, and independently interactive objects. This can reduce draw overhead
   without reducing geometry quality, but needs rebaking and measured results.
3. Test texture streaming on the largest maps if memory/loading is a problem. It is disabled
   globally and imported textures are NeverStream, so changing only r.TextureStreaming does
   not address those assets. Keep UI/font textures resident. Streaming can reduce residency;
   it also adds management work and can show lower mips while loading, so it is not a guaranteed
   quality-neutral FPS improvement.
4. Keep Lumen, Nanite, virtual shadow maps, and ray tracing off for this baseline. The current
   small meshes, baked lighting, SM5/mobile targets, and simple materials do not establish a
   reason to enable them. Mesh/animation LOD changes also need quality comparison, not blanket
   enabling of a reduction plugin.

The initial audit above was a read-only code/configuration assessment, not a benchmark of proposed changes.
Primary evidence came from the installed UE 5.8.4 template sources and .uplugin descriptors,
DF module dependencies, DFCharacter, DFVehicle, DFLevelBuilder, and DFImporter. Online Epic
documentation was unavailable during the audit (search connector errors and HTTP 403).
No runtime configuration or asset content was changed for this assessment.


## Systems follow-up (2026-10-29)

This follow-up inspects the supplied `VehicleProject`, local Starter Content, UE 5.8.4
source and converted map manifests. It does not establish runtime performance gains.
The user released the shared Unreal build lane on 2026-10-30. The validation results below
separate measured changes from the remaining experiments.

### Bots: retain Unreal navigation; do not migrate for an assumed speed gain

`DFBotController` already uses `AAIController`, navigation/path following, focus and timers.
Its main decision loop runs every 0.2 seconds. Enemy selection scans characters and performs
visibility traces; vehicle steering still updates each frame. StateTree would organize these
behaviors but would not replace their sensing, weapon, objective or steering implementations.
The original project dependencies were only two StateTree module entries in Build.cs.

The installed StateTree component supports scheduled ticks, enabled by default through
`StateTree.Component.ScheduledTickEnabled`. It is not inherently a per-frame polling system.
Its visual state organization and debugger may help larger behavior sets, but a performance
claim requires equivalent behavior and measurements. See Epic's
[StateTree overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/state-tree-in-unreal-engine).

The asset registry scan found 38,387 assets, no StateTree assets, and no referencers of
`/Script/StateTreeModule` or `/Script/GameplayStateTreeModule`. Both plugins are now disabled
and their module dependencies removed. The Linux editor build and six gameplay suites passed;
the resulting build receipt has no StateTree entries. ChunkDownloader remains enabled.
This is dependency cleanup, not evidence that StateTree itself is slow.

CPU captures on `mf-warehouse`, NullRHI, 60 FPS cap, 20-second samples after a 9-second warmup:

| Requested bots | Mean Think time/frame | 95th percentile | Mean enemy-selection time/frame |
|---|---:|---:|---:|
| 6 | 0.041 ms | 0.276 ms | 0.025 ms |
| 24 | 0.272 ms | 2.071 ms | 0.182 ms |
| 48 | 0.773 ms | 6.367 ms | 0.550 ms |

These scopes include nested work; do not add Think, FindEnemy and SightTrace together.
NullRHI's CSV `GameThreadTime` was zero, so it is not used as a total CPU measurement.
None of these warehouse runs logged spawn failures. Live combat, respawns and asset loading
vary between runs; these are exploratory measurements, not a deterministic StateTree A/B test.

The original 0.3-second initial timer delay aligned bots spawned together. At 48 bots, 101 of
1,130 frames spent over 2 ms in Think, mostly recurring every 11–12 frames. Initial delay now
varies from 0.2 to 0.4 seconds; the recurring interval remains 0.2 seconds per bot. Two repeat
48-bot captures measured 95th-percentile Think costs of **1.656 and 1.393 ms**, with means of
0.929 and 0.844 ms. This spreads bursts; it does not reduce the amount of AI work or establish
an average CPU saving. Existing Unreal navigation and the MF combat behavior remain in use.

The CSV / Insights timing scopes used for these captures (`DFBots`: decisions, enemy
selection, sight traces, steering; `DFVehicles`: wheel simulation) were temporary and are kept
in the verified `stash-backups/stash-0.bundle` recovery archive beside the test artifacts,
not in the source. Both reviewed stash entries were dropped after archiving.

### Animation: Budget Allocator tried and removed

A 2026-10-30 experiment put the third-person soldier mesh on `USkeletalMeshComponentBudgeted`
behind `a.Budget.Enabled` (first-person arms and dedicated servers outside it). A 48-bot
warehouse off/on/off capture at 1024×576 measured the budgeted animation scope at
0.224 / 0.017 / 0.224 ms per frame, but overall game-thread means stayed near 22.5–22.7 ms:
no measurable frame-time gain (the existing URO and `OnlyTickPoseWhenRendered` already skip
offscreen soldiers). The plugin, the component switch and its test were removed again; the
work is retained in that recovery bundle. See Epic's
[allocator guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-budget-allocator-in-unreal-engine)
before trying it again with a visible-crowd benchmark.

Earlier `MF_TestRange` captures are excluded: respawning initially reset the camera, and the
arena's saved map reported missing/rated player starts and repeated spawn failures under TDM.
On 2026-10-30, a read-only map audit found 15 actors and no player starts. The map lost its
MFPlayerStart, MFMapInfo and loadout-crate exports during the earlier redirect-removal resave
(`85a6eef`); the maintained generator still contained the correct actors. Regenerating through
`mfport.py import --steps testmap` restores four neutral starts, two per team, the loadout
crates, map information and navigation. Lighting was rebaked with zero errors/warnings.
The new `DesktopForces.Maps.TestRange.TeamStarts` regression test failed on the old asset and
passes on the regenerated one, checking both teams' selection and navmesh under every start.
A 30-second offscreen TDM run retained 24 bot controllers with no missing-start warnings
and no unresolved respawns. One occupied start needed three collision retries before spawning
successfully. No compatibility redirects or runtime fallback starts were added.

### Vehicles: a useful suspension reference, not a drop-in pawn replacement

The supplied template uses `AWheeledVehiclePawn` and `UChaosWheeledVehicleMovementComponent`.
Its sports car has torque/gear curves and Ackermann steering. DF already uses Chaos rigid
bodies, but implements wheel raycasts, spring/damper forces, tyre forces and drivetrain logic
in `DFVehicle::SimulateWheels`. That custom force model is the meaningful comparison target.

Chaos Vehicles supports runtime wheel radius, friction, engine contribution and suspension
changes (`SetWheelRadius`, `SetWheelFrictionMultiplier`, `SetAffectedByEngine`,
`SetSuspensionParams`). Wheel damage is therefore not an inherent blocker. Migration still
needs a skeletal vehicle/physics rig and validation of MF seats, damage, nitro, weapons and
network behavior. Boats and helicopters need separate treatment. Epic's
[vehicle setup guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-set-up-vehicles-in-unreal-engine)
describes the asset requirements.

DF applies wheel forces from game-thread Tick and clamps the supplied timestep to 0.05 s.
Test acceleration, braking, bumps and rollover recovery at 30/60/120 FPS before changing it.
Compare matched mass, gravity, wheel geometry and terrain: MF gravity is -2137.5 cm/s², while
the template uses the usual -980 baseline. Comparing their raw FPS or handling on unrelated
maps would not isolate vehicle simulation. A physics-thread suspension experiment is worth
considering if the current force model proves frame-rate-sensitive. No migration is justified
by the source comparison alone.

The supplied VehicleProject now builds with UE 5.8.4 (`-NoUBA -MaxParallelActions=2`), without
source or config edits. Three bounded NullRHI runs used its stock SportsCar, no steering and
five seconds of throttle after two seconds settling. Recorded straight-line displacements
were 55.82 m at 30 FPS, 54.89 m at 60 FPS, and 50.98 m at 120 FPS; sample intervals were
4.999 / 5.005 / 5.012 seconds. All runs exited normally. These single-run smoke samples vary
by about 9%, so the template defaults do not establish frame-rate-independent handling.
They also do not compare equivalent vehicles: the template's car, gravity, terrain and rig
differ from DF. Keep DF handling for now; use Chaos Vehicles as a reference for a matched
suspension prototype if handling measurements justify the conversion.

DF's existing content/vehicle fixture also passed at 30, 60 and 120 FPS. Two-second truck
turbo speed gains were 644.4 / 640.2 / 637.1 cm/s; the overturned Humvee recovered to
up.Z = 1.000 at every cap, with maximum sampled rotation steps of 6.4 / 5.9 / 1.7 degrees.
These exercise current DF handling and do not constitute a matched Chaos Vehicles migration
benchmark. The final current-code regression run passed all six suites (five gameplay plus
the new map-start test), and all 27 Python tests passed.

### Repeated static props: concrete candidates, not measured draw savings

The importer currently creates one StaticMeshActor per static-prop entry. An ISM/HISM
component can store many placements of a matching mesh/material, reducing component overhead
and potentially draw submissions while keeping the geometry. Interactive objects, movers and
independently destructible actors stay separate.

Offline counts group converted entries by mesh and collision flag. These groups are candidate
sets; material, lighting, transform and culling compatibility still need checking:

| Map | Static prop actors | Actors in groups of at least four | Example group |
|---|---:|---:|---|
| mf-Pobojowisko_VerII | 4,388 | 4,312 | kempaA: 1,169, no collision |
| mf-Tajga | 2,786 | 2,752 | iglakA: 586, collision |
| mf-Treasure_Finders | 2,720 | 2,590 | grassaw: 817 |

Unreal already merges compatible mesh draws; actor count is not draw-call count. Lightmap
bindings and per-component data can prevent merging. Use
`r.MeshDrawCommands.LogDynamicInstancingStats 1` with `stat RHI`, `stat SceneRendering` and
CPU/GPU captures to establish the baseline. See Epic's
[mesh drawing pipeline](https://dev.epicgames.com/documentation/en-us/unreal-engine/mesh-drawing-pipeline-in-unreal-engine).

Start with spatial groups of the non-colliding grass in one map. Preserve negative scales,
lighting, material variation and culling; rebake and compare the same camera path. Measure
render-thread time, draw submissions, scene/component memory and load time. No mass conversion
has been made.

### Replacement art: selective candidates for roadmap thread 9

Nine local Starter Content base-colour textures were exported in a scratch project before
the pause. Source sizes were 1024–2048 px; the staging tool produced a 512 px concrete sample.
`T_Concrete_Poured_D` was tested against `rage_warehouse.concrete.ware_concrete_9` in
`MF-RandomLand` using the same fixed camera before and after. It removes the original slab
lines and changes the floor's appearance too much for this substitution. The candidate was
rejected: the original material was restored byte-for-byte and the imported replacement asset
removed. `concrete-before.png` and `concrete-after.png` retain the comparison outside the repo.
No active local override remains. A slab/tile texture is a better candidate for this target.

Do not substitute by name alone: `rage_urban.brck07S` has transparent pixels that opaque
Starter brick would erase, and `rage_dock.ground.dock_ground_7` is a metal grate, not gravel.
Generic surfaces cannot replace signs, team symbols and foliage silhouettes indiscriminately.

Quixel's [Concrete Floor](https://www.fab.com/listings/f4b85cb0-9e15-42b5-997e-604cd09cbcd0)
listing was free when checked on 2026-10-29 and includes a base-colour texture. It is a candidate,
not an acquired or scene-tested asset. Download only a suitable source image after checking
the account's acquisition terms; do not import its full displacement/PBR material setup.
See [selected texture staging](Pipeline.md#selected-starter-content-or-acquired-textures)
for the local-only provenance workflow and 512 px cap. Imported replacement assets belong
under `/Game/MF/Replacements`, labelled community.

Evidence artifacts are outside the repository in
`/home/mrs/Documents/Unreal Projects/tests/20261029-systems-review/`:
`prop-candidates.json`, `missing-community-textures.json`, `texture-review.png`, the Starter
texture inventory/previews, and Python test output. The 34 unreplaced texture keys found in
converted community-map manifests are not a count of all remaining cooked classic dependencies.

For future CPU traces, keep one Unreal job active and test one change at a time. A bounded
capture can use:

```bash
python3 Tools/play.py mf-warehouse tdm 24 -nullrhi -nosound -unattended -trace=cpu,frame,bookmark -statnamedevents -DFQuitAfter=45
```

Repeat at 6 and 48 bots. NullRHI helps isolate CPU work but cannot assess visible animation or
GPU cost; repeat with `-RenderOffscreen` instead of `-nullrhi` for rendered tests. Warm assets,
keep camera/map settings fixed, and report game/render/GPU timings separately. CSV captures above
used the same launcher with scheduled `csvprofile START` / `STOP` (and the archived temporary scopes); the
trace command is provided for deeper follow-up.
