# Roadmap

Legend: ✅ done · 🟡 partial · ⬜ todo

## Foundation
- ✅ UE 5.8 project: forward/mobile rendering, no post, static lighting, ~80 plugins off, Low scalability defaults
- ✅ Git repo, open-source layout (code/tools/docs tracked, original assets generated locally)
- ✅ Docs for the team (this folder)
- ✅ Licence: MPL-2.0 ([LICENSE.md](../LICENSE.md))
- ⬜ CI build (Linux + Windows), automated import test on a small map

## Pipeline (`Tools/mfport`)
- ✅ Package reader (v83), BSP world geometry, static meshes, actors, lights, textures, sounds
- ✅ UE importer commandlet, map builder, Lightmass bake step
- ✅ Override folders for clean replacement textures/sounds
- ✅ Lightmass on Linux (UE 5.8.3): 6-byte patch of the shipped library (once per machine) + `-messaging` for the bake commandlet ([Pipeline.md](Pipeline.md#baking-on-linux-ue-583))
- 🟡 Light intensity tuning against the original look: baked warehouse now shows its textures (0.35 per light); spawn rooms still blow out where many lights overlap - compare with MF screenshots, maybe per-light radius/brightness curve
- ✅ Music (`.umx` Impulse Tracker modules → WAV via ffmpeg/libopenmpt, looping SoundWaves)
- ⬜ Skeletal meshes + animations (soldiers, first-person weapons): MeshAnimation parses; SkeletalMesh reader + PSK/glTF export
- ⬜ `DeviceMesh` / `LodMesh` (crates, pickups, effects)
- ⬜ Movers (doors/lifts are brushes with keyframes), water zones → volumes
- ✅ Ladders → `AMFLadderVolume` with nav links
- ✅ Navmesh baked by the importer; per-resolution step height 56 cm (MF stairs), drop-down links (experimental UE generator), 16k A* nodes
- ✅ Mod plugin art: `Plugins/Mods/*/SourceArt` → `mfport.py mods` → `import --steps plugins`
- ⬜ Sky: capture each map's SkyZone into a cubemap for the sky material
- ⬜ Minimap textures (MiniMapInfo)
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
- ✅ Safecracker (two parts, safe cracking, gold), CTF (`AMFFlag` shared by both)
- ⬜ Captains, Detonation, Trailer — data already in converted maps as `AMFLegacyActor`
- ✅ Chat (T/Y, Say/TeamSay), team orders menu (V/C) with bot orders, MF message list
- ✅ N teams (`UMFGameSettings::Teams`, `?Teams=`), UE match states, `ULocalMessage` announcements
- 🟡 Bots: travel the whole map now (the warehouse navmesh is fine; a 30 s travel budget made them give up halfway), unstick by jumping, direct approach to goals off the navmesh edge. Ladder links still unverified. Roster (32 named bots in config, numbered extras beyond), loadouts, aim/strafe/fire, objective goals (`?Bots=N?BotSkill=0.7`). `MFNavTest` / `MFNavPath X Y Z` print reachability and routes. Never rebuild the navmesh in the light bake (it came out empty).
- ⬜ Vehicles: Buggy, Humma, Truck, APC (reverse `handling.hnd2` / `.COL`; Chaos Vehicles or a light custom raycast model)
- 🟡 Ladders (LadderClimber → `AMFLadderVolume`, untested in game); ⬜ swimming/drowning, water zones
- ✅ Mods as plugins ([Modding.md](Modding.md)): `MFModMolotov` (weapon), `MFModKOH` (game mode)
- ⬜ More MF-mods: Totem, ZombieGame (as components, not player subclasses), TugGame, CoopMode, arenaMode, SentryGun, SeekerMines, TripLasers, PredatorMissile …

## Presentation
- ✅ MF HUD with the original RageHUD art (blue / red team sets, box segments per carry block, ammo bars, health/armour silhouettes, loadout icon, loadout emblem backdrop, HUD sizes 0-4); plain fallback without the art; MF bitmap fonts, pain flash, translucent crosshair, team score panels, Safecracker bars; ⬜ minimap, MF scoreboard
- ✅ MF loadout behaviour: frozen soldier and view, one cursor through store and carried items (put back from the inventory), W/S/wheel/Fire/E, number rows + X, sounds, spawn protection
- ✅ MF console commands and cheats ([Commands.md](Commands.md)), KillZ under every map
- ✅ Graphics options above potato via scalability levels (MSAA, anisotropy, Epic bloom)
- ⬜ First-person weapon models + animations, third-person soldiers, team skins
- ⬜ Effects: muzzle flash, tracers, impacts, explosions, blood (cheap sprite particles)
- ⬜ Menus: main menu, server browser (master server: 333networks protocol used by MF), options
- ⬜ Touch controls (mobile)

## Maps
- ✅ mf-warehouse converts and builds
- ⬜ All stock maps (airport, carpark, dockyard, ghetto, hydroworks, polar, prehistoric, rail quarry, sawmill, space station, toreford, tropics, waterfront, western …)
- ✅ mf-warehouse (original) is the map to play and build on
- ⬜ Add Mobile Forces Source features onto it by hand (secrets, `func_breakable` doors and passages, props) — the automatic geometry merge was tried and dropped: seams, holes and misaligned pieces ([WarehouseMerge.md](WarehouseMerge.md); the alignment and prop converter stay useful for placing MFS pieces)
