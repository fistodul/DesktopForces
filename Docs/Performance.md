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
