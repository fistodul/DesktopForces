# Roadmap

Legend: ✅ done · 🟡 partial · ⬜ todo

## Foundation
- ✅ UE 5.8 project: forward/mobile rendering, no post, static lighting, ~80 plugins off, Low scalability defaults
- ✅ Git repo, open-source layout (code/tools/docs tracked, original assets generated locally)
- ✅ Docs for the team (this folder)
- ✅ Licence: MPL-2.0 ([LICENSE.md](../LICENSE.md)); credits ([CREDITS.md](../CREDITS.md), in-game Credits page)
- ✅ Name: **Desktop Forces** (2026-10-20); the original game's converted content is the separate *Mobile Forces Classic* plugin, community maps whose authors agreed are part of the game ([packs](Pipeline.md#content-packs))
- ⬜ CI build (Linux + Windows), automated import test on a small map

## Pipeline (`Tools/mfport`)
- ✅ Package reader (v83), BSP world geometry, static meshes, actors, lights, textures, sounds
- ✅ UE importer commandlet, map builder, Lightmass bake step
- ✅ Override folders for clean replacement textures/sounds
- ✅ Lightmass on Linux (UE 5.8.3): 6-byte patch of the shipped library (once per machine) + `-messaging` for the bake commandlet ([Pipeline.md](Pipeline.md#baking-on-linux-ue-583))
- ✅ Lighting: UE1 light values (intensity ≈ π), sky windows transparent to light, 2 Lightmass bounces, soft light radii, small unshadowed floor from each zone's ambient (world vertex colour, `MPC_MF`) - all baked
- ✅ Music (`.umx` Impulse Tracker modules → WAV via ffmpeg/libopenmpt, looping SoundWaves)
- ⬜ Skeletal meshes + animations (soldiers, first-person weapons): MeshAnimation parses; SkeletalMesh reader + PSK/glTF export
- ✅ Rage `DeviceMesh` (vehicles, wheels, crates, safes, flag bases, det locks, holdout timers): `mfport.py props`
- ✅ `LodMesh` first frame (weapon carry models, rockets, grenades, det key); ⬜ Rage `CollisionMesh`, LodMesh animation
- ⬜ Movers (doors/lifts are brushes with keyframes), water zones → volumes
- ✅ Ladders → `AMFLadderVolume` with nav links
- ✅ Navmesh baked by the importer; per-resolution step height 56 cm (MF stairs), drop-down links (experimental UE generator), 16k A* nodes
- ✅ Our own art: `SourceArt/` (menu pictures, start-up theme, mod icons) + procedural textures → `Tools/artgen` → `/Game/MF/...`
- ✅ Content packs: classic (plugin) / core / replaced roots from `packs.toml`; core maps own their material instances (replacement art needs no re-bake)
- 🟡 Replacement art for the textures core maps borrow: generated stand-ins for ~850 (`Tools/artgen/lookalike.py --auto`); see-through ones (foliage, decals) still original
- ✅ Sky: each map's SkyZone captured into a cubemap for the sky material
- ✅ Minimap textures (MiniMapInfo)
- ✅ Textures straight from the packages: S3TC copies (MF's D3D look), palette alpha for HUD art
- ⬜ Texture arrays / trim sheets per map to cut draw calls
- ⬜ Zone-based level streaming / HLOD for big outdoor maps
- ✅ VMF (Source) converter → same formats (`mflib/vmf.py`, `vmf_materials.toml`), floor-plan tool (`debug/floorplan.py`)

## Gameplay
- ✅ Player movement (MF constants), crouch/walk, fall damage, carry-weight speed, recoil
- ✅ Weapons: knife, pistol, machine gun, shotgun (twin barrels), sniper (charge + variable scope), rocket launcher, grenades (timed/impact), adrenaline, armour
- ✅ Heavy machine gun (deploy, spin-up), trip bombs (laser + proximity mine)
- ⬜ Rocket lock-on (SmartRocket)
- ✅ Loadout crates/zones, carry blocks, refill on entering
- ✅ Weapon drops (throw / death) as ground items taken through the loadout (MF Loadout_Ground); loadout opens anywhere
- ✅ Deathmatch, Team Deathmatch
- ✅ Holdout (Domination): `AMFGameMode_Holdout` + `AMFHoldoutPoint`, captures, hold-time scoring, HUD readout, bot attack/guard goals
- ✅ Safecracker (two parts, safe cracking, gold), CTF, Detonation (`AMFFlag` shared), Captains
- ✅ Trailer: cab + hitched bomb trailer, delivery bays, countdown, detonation
- ✅ Chat (T/Y, Say/TeamSay), team orders menu (V/C) with bot orders, MF message list
- ✅ N teams (`UMFGameSettings::Teams`, `?Teams=`), UE match states, `ULocalMessage` announcements
- 🟡 Bots: travel the whole map now (the warehouse navmesh is fine; a 30 s travel budget made them give up halfway), unstick by jumping, direct approach to goals off the navmesh edge. Ladder links still unverified. Roster (32 named bots in config, numbered extras beyond), loadouts, aim/strafe/fire, objective goals (`?Bots=N?BotSkill=0.7`). `MFNavTest` / `MFNavPath X Y Z` print reachability and routes. Never rebuild the navmesh in the light bake (it came out empty).
- 🟡 Vehicles: Buggy, Humvee, Truck, Armadillo APC as physics boxes with raycast wheels tuned from `handling.hnd2` / `.COL` (springs scaled to MF gravity), MF meshes, generators, enter/exit (E), run-over, explosives-only damage. ✅ passenger seats (shooting from them), Armed APC turret; ⬜ bots driving / riding, wheel blow-outs
- ✅ Ladders: grab when close / falling in, climb down from the top, floor walks again; ⬜ swimming/drowning, water zones
- ✅ MF-mods, part of the game (`Source/DesktopForces/Mods`, [Modding.md](Modding.md)): Molotov, KOH, Totem, TripLasers, SeekerMines, SentryGun, TugGame, CoopMode, ZombieGame, deagle, grenLaunch, PredatorMissile, ArmedAPC, arenaMode
- ⬜ Mods for the packaged game: a `Mods/` folder of paks scanned at start, mod maps in the menu
- ⬜ More MF-mods: jeep, boat, heli, battleRoyale, MoBall, mutPack, vote, TurboCars

## Presentation
- ✅ MF HUD with the original RageHUD art (blue / red team sets, box segments per carry block, ammo bars, health/armour silhouettes, loadout icon, loadout emblem backdrop, HUD sizes 0-4); plain fallback without the art; MF bitmap fonts, pain flash, translucent crosshair, team score panels, Safecracker bars, minimap (MF MiniMapInfo texture, team border, team-mates, objectives); ⬜ MF scoreboard
- ✅ MF loadout behaviour: frozen soldier and view, one cursor through store and carried items (put back from the inventory), W/S/wheel/Fire/E, number rows + X, sounds, spawn protection
- ✅ MF console commands and cheats ([Commands.md](Commands.md)), KillZ under every map
- ✅ Generated art of our own (`Tools/artgen`): loadout crates with team stencil
- ✅ Graphics options above potato via scalability levels (MSAA, anisotropy, Epic bloom); texture quality Low samples 256 px
- 🟡 First-person weapons: MF carry models held in view (no arms / animations yet: those are SkeletalMeshes); ⬜ third-person soldiers, team skins
- ⬜ Effects: muzzle flash, tracers, impacts, explosions, blood (cheap sprite particles)
- 🟡 Menus: title screen over the MFS pictures with the start-up theme, play / join / options (render scale, brightness, mouse, FOV …) / credits / quit, Esc menu in game; ⬜ server browser (333networks master server protocol)
- ⬜ Touch controls (mobile)

## Maps
- ✅ mf-warehouse converts and builds
- 🟡 More maps convert and bake: Carpark, Toreford, MF-Filip, mf-Microwarehouse, Mf-MicroBunker, mf-MicroTower. ⬜ the rest (airport, dockyard, ghetto, hydroworks, polar, prehistoric, rail quarry, sawmill, space station, tropics, waterfront, western …)
- ✅ mf-warehouse (original) is the map to play and build on
- ⬜ Add Mobile Forces Source features onto it by hand (secrets, `func_breakable` doors and passages, props) — the automatic geometry merge was tried and dropped: seams, holes and misaligned pieces ([WarehouseMerge.md](WarehouseMerge.md); the alignment and prop converter stay useful for placing MFS pieces)
