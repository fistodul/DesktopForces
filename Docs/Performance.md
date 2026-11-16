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
- Texture streaming off (textures are ≤ 1024 px, `NeverStream`). Texture quality Low samples
  one mip down (`r.MipMapLODBias=1`: 512 px textures draw as 256); `max_texture_size = 256` in
  `mfport.toml` imports everything at 256 for the smallest memory. A converted MF map's
  textures are ~50 MB at 512 px, so 4 GB VRAM cards have lots of room.
- Lighting is all baked: two Lightmass bounces, soft light radii, sky light through sky
  windows - none of it costs anything at runtime. The only live lighting term is one
  multiply-add in the lit materials (`BaseColor × MPC_MF.Ambient`, the map's ambient floor).

## Content
- Textures imported at original resolution up to 512 px (`max_texture_size`), or 1024 px when
  listed in `Tools/mfport/detail.toml` (weapons, walls seen up close; Detail group, 512 px on
  phones); strips of 4:1 or longer keep their short side (Strip group, 1024 px long on phones);
  Android/iOS
  device profiles cook World/UI textures down to 256 px. No alpha channel unless masked
  (half the memory with BC1/ETC2).
- Navmesh is baked into each map by the importer (no runtime generation).
  `AverageLayersPerTile=7` (engine default 3) sizes the tile pool for maps with stacked floors:
  the warehouse maps need about 6, mf-Jungle 4. A too-small pool drops tiles ("tile limit
  reached" in the import log) and bots get no mesh there. Unused slots cost ~100-200 bytes each.
- Level geometry is merged into ~2048 uu (46 m) grid chunks, one draw per material per chunk;
  collision lives in separate invisible meshes so render meshes carry no physics.
- Lightmap resolution follows surface area (16 uu texels), capped at 1024 (props too: some maps'
  terrain is one static mesh). Lightmap UVs: the better of two layouts, see
  [Pipeline](Pipeline.md#lights).
- Texture arrays: tried, no gain. Trim sheets and HLOD / zone streaming: held back, see
  [the measurements](#arrays-and-streaming-measured-2026-11-01).

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
FPS, VSync and render scale are in Advanced Graphics, and so are the three groups that change
the cost or the picture (measured below): anti-aliasing, textures and effects, each on a level
of its own. Moving one off the preset marks the preset with a star ("Medium*"); picking a
preset (that one too) sets every group to it again. The groups the menu doesn't show follow the
preset (`sg.ViewDistanceQuality` holds which one); set apart at the console they make it
"Medium*" as well. VSync and the FPS cap are
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


## Graphics options: measured 2026-11-14

`python3 Tools/mfport/debug/gfxbench.py <out dir>` (`--res=3840x2160`, `--reverse`): every
scalability group one level at a time from Medium, render scale, and the presets, at three fixed
cameras (mf-Sardine_City from a roof and in a street, MF-Test_Facility's hall), no bots,
uncapped, editor binary with `-game`, rendering off screen. Three runs at 1920 x 1080 and two at
3840 x 2160, one of each with the settings in reverse order; GPU ms is the mean over scenes and
runs. The picture column is the mean grey-level difference to Medium's screenshot (of 255); two
Medium shots differ by 0.3.

Conditions, logged every 2 s by the script: RTX PRO 500 laptop GPU (35 W), Core Ultra 7 265H, on
mains throughout, `balanced` power profile, `powersave` governor. Every scene started cooled to
GPU 58 C / CPU 65 C and warmed up through its 4.5 minutes: GPU to 68 - 79 C at 1080p and 80 -
87 C at 2160p, the CPU package to 72 - 89 C at the captures with peaks of 105 C between them
(its limit: an uncapped frame rate keeps one core flat out). At 1080p the GPU ran 30 - 80 %
busy at 2500 - 2700 MHz, power-capped in 2 - 5 % of the samples; at 2160p 97 % busy and
power-capped in about 70 % of them, its clock falling from 2500 to about 2200 MHz as it warmed.
The two Medium rows of a scene, first and last, differ by 2.4 % in GPU time on average (6 % at
most) and by up to 16 % in frame time: that is the error bar. The reversed runs cancel the
warm-up in the means.

| Setting | GPU ms 1080p | | GPU ms 2160p | | Picture |
|---|---|---|---|---|---|
| Medium (2x MSAA, 2x anisotropy) | 1.18 | | 3.23 | | |
| Anti-aliasing 0: none | 0.97 | -18 % | 2.05 | -37 % | 0.4 - 0.6 |
| Anti-aliasing 2 / 3: 4x | 1.27 | +8 % | 3.71 | +15 % | 0.2 - 0.4 |
| Textures 0: one mip down, no anisotropy | 1.17 | 0 | 3.18 | -1 % | 2.1 - 3.7 |
| Textures 2 / 3: 4x / 8x anisotropy | 1.17 / 1.18 | 0 | 3.23 / 3.24 | 0 | 0.4 - 0.6 |
| View distance 0 / 2 / 3 | 1.16 / 1.17 / 1.17 | 0 | 3.23 / 3.24 / 3.23 | 0 | 0.3 |
| Effects 0 / 3 | 1.17 / 1.25 | 0 / +7 % | 3.23 / 3.55 | 0 / +10 % | 0.3 / 0.5 |
| Post-processing 0 / 3 | 1.17 / 1.16 | 0 | 3.24 / 3.23 | 0 | 0.3 |
| Shadows 0 / 3 | 1.10 / 1.17 | -7 % / 0 | 3.17 / 3.22 | -2 % / 0 | 0.3 |
| Global illumination, reflections, shading 0 / 3 | 1.17 - 1.19 | 0 | 3.22 - 3.24 | 0 | 0.3 |
| Render scale 50 % | 0.95 | -19 % | 1.54 | -52 % | 2.3 - 4.3 |
| Render scale 75 % | 1.00 | -15 % | 2.40 | -26 % | 1.9 - 2.6 |
| Render scale 150 % | 2.05 | +74 % | 8.55 | +164 % | 1.0 - 1.8 |
| Low preset | 0.96 | -19 % | 1.96 | -39 % | 2.1 - 3.9 |
| High preset | 1.29 | +10 % | 3.69 | +14 % | 0.4 - 0.8 |
| Epic preset | 1.40 | +19 % | 4.09 | +27 % | 0.5 - 1.0 |

What it says:
- **The frame is not the GPU's here.** 4.3 to 8.5 ms a frame at both resolutions and every
  setting but 150 % of 2160p; the render thread takes 4.5 - 7 ms of it, the GPU 1 - 4. No
  setting moves the frame time by more than two captures of the same setting differ (heat:
  the hotter CPU decides), so only the GPU column tells settings apart, and a weak GPU scales
  that column.
- **MSAA is the one costly quality option**, and its first step is the dear one: none to 2x is
  +22 % at 1080p and +58 % at 2160p, 2x to 4x another +8 % / +15 %.
- **Anisotropic filtering is free** on this GPU (2x, 4x and 8x within 0.01 ms).
- **Low's mip bias buys nothing here**: the same GPU time as Medium's textures for by far the
  largest loss of picture of any option (2.1 - 3.7; it saves no memory either, the mips stay
  loaded). Low's gain is its MSAA being off.
- **View distance, global illumination, reflections, shading and post-processing change
  neither time nor picture**, shadows next to nothing (Shadows 0: -0.07 ms, same picture):
  lighting is baked, shading forward, post-processing off. The presets differ by MSAA,
  anisotropy, the mip bias and Epic's bloom and effects.
- **Render scale is the lever for a weak GPU**: 75 % takes 26 % off at 2160p, 50 % takes 52 %
  (at 1080p this GPU's fixed costs hide most of it).
- **An uncapped frame rate cooks the laptop**: the CPU package reaches its 105 C limit within
  minutes at 120 - 230 FPS. Its panel shows 60; how warm it runs at the default 120 FPS cap or
  at 60 was not measured.

**The integrated GPU** (Intel Arc of the same Core Ultra 7 265H, Mesa Vulkan driver, chosen
with `--gpu=intel`; 2026-11-14, one forward and one reversed run at 1080p, same conditions,
mains): about three times slower than the NVIDIA GPU, and the same pattern.

| Setting | GPU ms, Intel 1080p | |
|---|---|---|
| Medium | 4.20 | |
| Anti-aliasing 0 / 2 | 3.59 / 5.09 | -14 % / +21 % |
| Textures 0 / 2 / 3 | 4.21 / 4.21 / 4.11 | 0 / 0 / -2 % |
| Effects 3 | 4.44 | +6 % |
| View distance, shadows, GI, reflections, shading, post | 4.07 - 4.19 | -3 - 0 % |
| Render scale 50 / 75 / 150 % | 3.50 / 3.63 / 6.89 | -17 / -14 / +64 % |
| Low / High / Epic | 3.71 / 5.06 / 5.84 | -12 / +20 / +39 % |

**Textures, the mip bias.** A texture is kept with its mips: copies at half the size, a quarter,
and so on down to 1 pixel (a third more memory than the picture alone). The GPU picks the copy
whose texels match the pixels on screen; `r.MipMapLODBias=1` (Low's textures) makes it pick the
next smaller one: half the sharpness, in the hope of less memory traffic. Here it saves
nothing: the same GPU time on both GPUs (4.21 against 4.23 ms on the integrated one, which
shares the CPU's memory and should feel memory traffic first), and the same memory, 128.5 MB of
textures on MF-Test_Facility at Low, Medium and Epic textures alike (`ListTextures`: with
streaming off every mip stays loaded whatever is sampled). What does take memory off is loading
smaller textures: `max_texture_size = 256` at import (everything at 256 px, a quarter of the
memory and download), or a device profile's texture group sizes (Android and iOS already cook
world textures down to 256). Anisotropic filtering (sharp surfaces seen at a slant) cost
nothing measurable either: within 0.1 ms at 8x on both GPUs.

Proposed defaults. The texture column is in the code (`UDFUserSettings::PresetQuality`, the
presets' textures one level up; Cinematic 16x), to confirm with the phone run; anti-aliasing,
effects and post stay as they are. Advanced Graphics' "Texture filtering" offers Blurry (half-size
mips, `bHalfSizeTextures`, no filtering), Off, 2x, 4x, 8x and 16x; no preset picks Blurry or Off,
and a choice off the preset shows a star ("Medium*"):

| Preset | Anti-aliasing | Textures now | Textures proposed | Effects | Post |
|---|---|---|---|---|---|
| Low | off | half-size mips, no filtering | full size, 2x filtering | Low | off |
| Medium (desktop default) | 2x | 2x filtering | 4x filtering | Medium | off |
| High | 4x | 4x filtering | 8x filtering | High | off |
| Epic | 4x | 8x filtering | 8x filtering (same) | Epic | bloom |

| Change | GPU cost (NVIDIA / Intel) | Picture | Memory |
|---|---|---|---|
| Low: full-size mips instead of half | 0 / 0 | sharper (Low's picture comes to Medium's minus MSAA) | none |
| Low: 2x anisotropy instead of none | 0 / 0 | sharper at a slant | none |
| Medium 4x, High 8x anisotropy (one step up) | 0 / 0 | a little sharper at a slant | none |
| MSAA left as it is | 2x: +22 % / +18 %, 4x: +8 % / +21 % more | smooth edges | render targets |

Also proposed: a GPU that can't hold the frame rate at Low lowers render scale (75 %, then
50 %) before anything else; and to try, after measuring the temperatures, new desktop profiles
capped at the display's refresh rate instead of 120 (this laptop's panel is 60 Hz).

Not measured: effects quality in a fight (a still scene has few particles; the +7 % at
Effects 3 is the maps' own emitters), Epic's bloom (the game turns it on from the saved
settings, which the script doesn't touch), a Shipping build's render thread, the `performance`
power profile (the phone: below). Traps met: the saved window size beats `-ResX` / `-ResY` (a
first try measured 888 x 500: the script sets `r.SetRes` and checks its screenshots' size);
nvidia-smi's "Clocks Event Reasons Counters" wrap every 10 s (the script reads the per-reason
flags instead).

### On a phone (Pixel 9a, 2026-11-14)

`gfxbench.py <out> --android`, Android Development build, the same three cameras, each scene
started cooled (GPU 33 - 35 C, skin 30.5 - 32 C), off the charger (battery 71 to 63 %), power
saver off, brightness low (auto), the panel at 120 Hz while the game ran (the owner's Smooth
Display setting, 60 Hz, put back after), 1616 x 720 (content scale 1). Thermal status stayed
0 throughout (GPU 45 - 53 C, CPU up to 71 C, skin up to 35.8 C over about 5 minutes a scene).

| Scene | Frame | Game thread | Render thread | GPU Medium | AA off | AA 4x | Textures off - 16x, Blurry |
|---|---|---|---|---|---|---|---|
| facility_hall | 8.36 | 5.88 | 6.10 | 4.25 | 4.01 | 4.38 | 4.20 - 4.29 |
| sardine_roof | 8.33 | 5.81 | 7.78 | 4.41 | 4.13 | 4.56 | 4.39 - 4.43 |
| sardine_street | 8.36 | 5.58 | 6.52 | 4.54 | 4.32 | 4.87 | 4.51 - 4.59 |

(ms, medians.) The phone holds 120 FPS (8.33 ms: the panel's rate, the most a phone shows) at
every setting with the GPU half idle; the threads, not the GPU, are closest to the limit. MSAA
is again the one setting with a cost (2x about 6 % of GPU time); texture filtering and half
size, view distance, shadows, GI, reflections, shading and post-processing are within 0.1 ms.
Rows where the picture itself changed (a difference of 60 - 85 against Medium: something moved
into the view on Sardine City, from Effects 0 on in sardine_street, which then rose to 12 -
17 ms) are the scene's, not the setting's. Render scale did nothing: without mobile HDR there
is no upscale pass, `r.ScreenPercentage` is ignored (the same GPU time at 50 and 150 %); it now
sets the content scale on Android instead (the whole surface, HUD included: 0.5 = 808 x 360,
1.5 = the panel's 2424 x 1080), which the engine applies at run time. The texture defaults
(one level up) hold on the phone too; phones keep the Low preset (AA off) until a weaker phone
is measured. Traps met: on a 60 Hz panel every frame reads 16.67 ms (the script sets 120 Hz for
the run); `dumpsys thermalservice` lists cached temperatures before the live ones (the script
reads only "Current temperatures from HAL"); quitting right after the last capture leaves its
CSV empty (15 s margin now).

Seen in played rounds on the phone (TDM, 6 bots, touch input, `-LogCmds="LogStreaming Verbose"`
names each load): 169 synchronous loads (`FlushAsyncLoading`), 149 of them in the first 5 s
(HUD textures, the soldier's and weapon's meshes, animations and sounds, loaded by path as the
player spawns), then about 20 on first use: a vehicle's flag (mesh, material, texture, four
animations) and engine sounds as the first vehicle comes near, knife animations on the first
stab or jump, single sounds. Each sits next to a failed memory map of the small bulk chunk read
from the OBB (`OpenMappedEx failed ... pakchunk3`: the engine falls back to a read). A whole
round's CSV (60 Hz panel): median 16.7 ms, p99 33 ms; in play a frame over 25 ms only every
few seconds (26 - 29 ms, once 61 ms), so a first-use load costs at most a frame or two; the
spawn second has the worst (66 ms). Preloading the classes' path-loaded assets (vehicle flag,
weapon animations) at spawn would take the in-play ones out; not worth it yet.
One engine ensure a session: `!IsComponentPSOPrecachingEnabled() || bPSOPrecacheCalled`
from the Cascade pool reusing a `UParticleSystemComponent` with a new template
(`UGameplayStatics::SpawnEmitterAtLocation`, `EPSCPoolMethod::AutoRelease`); ensures are off in
Shipping, so left as it is.

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

## Arrays and streaming: measured 2026-11-01

All 86 maps ran with 6 bots at 1280 x 720 on Linux (`-game`, editor build, RTX PRO 500 laptop):
no crash, median frame 7.8 ms, 95th percentile across maps 13.8 ms, slowest 19.6 ms, median 56 and
at most 383 draws per frame. All 15 game modes ran with 8 bots without a warning. A test of one mip
down on four maps at 1080p with 8 bots showed no cost from the 1024 px textures, but its CSV
windows were too short and uneven to prove more than that: use a fixed camera path and a longer
capture for a real number. A cooked build and a Pixel 9a run are still missing.

An offline estimate over 87 maps (world geometry, whole map, not per frame) leaves this share of
today's world draws: 68 % with one texture array, 55 % with two, 50 % with three. The median map
has 38 world textures: 14, 26 and 29 of them fit those arrays, the rest stay normal materials
(see-through, additive, unlit, odd sizes). Arrays need one size and format each, so one per common
group (usually 512, then 1024). Some maps gain little (mf-The_Bank: 627 to 448), mf-Toreford gets
it all from one (739 to 266).

### Texture arrays tried on the two largest maps (2026-11-01): no gain, kept off

The code (`texture_arrays = N` in `map_edits.toml`, DFImporter `PlanTextureArrays`, `M_MF_OpaqueArray`)
was removed on 2026-11-06; it is in the history before that. mf-The_Bank (2 arrays, 27 textures) and mf-nc (2 arrays, 262 textures), same four
cameras, no bots, 1920x1080, 1200 frames, median / p95:

| Map | | Draws | Frame ms | Render thread ms |
|---|---|---|---|---|
| mf-The_Bank | single textures | 257 / 269 | 13.0 / 20.5 | 12.1 / 20.5 |
| mf-The_Bank | arrays | 259 / 269 | 12.1-12.3 / 19.7 | 11.4-11.8 / 19.8 |
| mf-nc | single textures, unbaked | 243 / 439 | - | - |
| mf-nc | arrays | 243 / 407 | 14.9-16.0 / 24.3-26.9 | 12.2-13.1 / 22.2-22.7 |

Draws did not move at the median, the peak on mf-nc fell 7 %, frame time stayed inside the spread of
repeat runs. The arrays add 59 MB (mf-nc) and 5.3 MB (mf-The_Bank) on disk, a copy of textures the
other maps still need, plus an extra UV channel on every vertex, and need a rebake to switch. The
offline estimate above counted every section of a map; a real view draws few of those. Draws are
not what limits this game on PC. Not measured on a phone: none was connected.

## Dark surfaces: what worked (2026-11-01)

Measured on mf-waterfront and mf-warehouse, no rebake unless noted:

| Change | Result |
|---|---|
| 4 Lightmass bounces instead of 2 (rebake) | under 1.2 % brighter at four cameras, 15 % more bake time: not used. On ZM-SpaceZombura (dark, zone ambient 0, 2026-11-16): 5.4 % brighter at 20 cameras for the same bake time; not decided ([Roadmap](Roadmap.md) thread 24) |
| `AmbientFloor` 0.35 to 0.7 | pixels under 30 of 255 in warehouse views: 20.6 % to 2.8 %, 10.4 % to 3.8 %; none blown out |
| `ShadeFloor` 0.1 (MPC, one add) | world polygons in zones with no ambient |
| `MovingActorLight` 0.4 (custom primitive data 0, one add) | vehicles, pickups, flags: they get no baked light at all |

Frame time was not measured for these; the material cost is two adds. Runs of the same map differ by
several ms here, so a change this small can't be seen in them.

## Instanced props (2026-11-02)

`instance_props = N` in `map_edits.toml`: props without collision placed N or more times in an 80 m
cell become one instanced static mesh component there (DFLevelBuilder). By cell because
visibility and lightmaps work per component: one component per mesh for the whole map left most of
them unbaked and near black. Props with collision too since 2026-11-04: their near-black bake as
instances was the overlapping lightmap UVs ([Pipeline](Pipeline.md#lights)). Needs a rebake of the map.

mf-Pobojowisko_VerII, 4388 props, four fixed cameras, no bots, 1500 frames, median / p95:

| | Mesh components | Draws | Triangles | Frame ms | Game ms | Render ms | Bake |
|---|---|---|---|---|---|---|---|
| separate actors | 5055 | 359 / 537 | 12369 / 39449 | 8.4 / 14.3 | 6.3 / 11.4 | 7.9 / 14.2 | 34 min |
| `instance_props = 8` | 2408 | 326 / 418 | 5889 / 17909 | 8.4 / 13.8 | 5.9 / 10.6 | 8.1 / 13.5 | 18 min |

Same image at all four cameras (mean difference 0.3 to 0.6 of 255). Frame time at the median doesn't
change on a PC: the gain is half the triangles, a fifth fewer peak draws and half the components.
Not measured on a phone. It's the default now (8, `mapedits.py`): other maps get it when rebuilt (Roadmap).
The masters (`M_MF_*`) need the instanced static mesh usage flag, set by the importer
(`FixMasterMaterialUsage`, every `-run=DFImport`): without it the game drew every instanced prop
with the default material (dark grey checks: mf-western's church benches, black barrels) until
2026-11-06. The editor sets missing flags itself, so it only showed in game.

When a lighting build starts, the editor module finishes mesh compilation and refreshes every
instanced component's bounds (DesktopForcesEditor.cpp). Registered while their mesh was still
compiling as the map loaded, instanced components kept zero bounds at the world origin when the
bake gathered their lights: they got the sun but none of the point lights near them, and the game reported
those as unbuilt ("Lighting needs to be rebuilt" and preview shadows; mf-western's benches,
warehouse, sawmill, Toreford; 2026-11-04). Maps baked before that need baking again.
`DumpUnbuiltLightInteractions` in the console names what is unbuilt.
Collision-only world chunks (hidden) take volumetric lighting: no lightmap is baked for them.

