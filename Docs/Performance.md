# Performance rules

Target: phones and potato PCs (reference dev machine: 6 GB VRAM laptop GPU). The desktop
build uses mobile-class settings too.

## Rendering (Config/DefaultEngine.ini)
- **Forward shading**, MSAA (scaled by AA quality: off / 2× / 4×). No TAA/TSR.
- **No post-processing** by default: bloom, AO, auto exposure, motion blur, lens flare, DOF
  off at Low/Medium/High (`DefaultScalability.ini`). Epic post quality adds a little bloom.
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
- Texture streaming off (textures are ≤ 512 px, `NeverStream`), no mip bias games needed.

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
- Actors don't tick unless they must. `AMFCharacter` ticks only when locally controlled
  (camera FOV + recoil); weapons, projectiles, crates, map actors never tick — timers only.
- Canvas HUD (no UMG widget tree to tick/layout).
- Skeletal meshes: `OnlyTickPoseWhenRendered`.
- Net: 30 Hz server tick, weapons 10 Hz net update, characters 30 Hz.
- Prefer `TActorIterator` only at events (spawn, loadout), never per frame.

## Plugins
~80 default plugins disabled in `MobileForces.uproject` (media players, ArchVis, PCG,
MetaHuman, Paper2D, cloth, ControlRig extras, source control providers …). Only disable a
plugin if nothing enabled depends on it — the list was computed from the `.uplugin`
dependency graph.

## Editor
`Config/DefaultEditorSettings.ini` starts the editor viewport at scalability Low. UBT:
if builds hang with `clang++` stuck in "D" state, kill leftover clang processes from an
aborted build (they wait on a dead UBA session); `-NoUBA` disables UBA detours.
