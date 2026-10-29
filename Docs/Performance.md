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

This was a read-only code/configuration assessment, not a benchmark of proposed changes.
Primary evidence came from the installed UE 5.8.4 template sources and .uplugin descriptors,
DF module dependencies, DFCharacter, DFVehicle, DFLevelBuilder, and DFImporter. Online Epic
documentation was unavailable during the audit (search connector errors and HTTP 403).
No runtime configuration or asset content was changed for this assessment.
