# Roadmap

Legend: ✅ done · 🟡 partial · ⬜ todo

## Foundation
- ✅ UE 5.8 project: forward/mobile rendering, no post, static lighting, ~80 plugins off, Low scalability defaults
- ✅ Git repo, open-source layout (code/tools/docs tracked, original assets generated locally)
- ✅ Docs for the team (this folder)
- ⬜ Licence choice for the public repo (MF-mods uses MIT)
- ⬜ CI build (Linux + Windows), automated import test on a small map

## Pipeline (`Tools/mfport`)
- ✅ Package reader (v83), BSP world geometry, static meshes, actors, lights, textures, sounds
- ✅ UE importer commandlet, map builder, Lightmass bake step
- ✅ Override folders for clean replacement textures/sounds
- ⚠️ **Lightmass is broken on Linux in UE 5.8.3**: the shipped `UnrealLightmass` binary asserts at startup ("The Gamethread can only be tagged on the inital thread"), so `mfport.py light` bakes nothing on Linux. Plan: own UE1-style lightmap baker in MobileForcesEditor (raycast shadows, MF falloff + zone ambient, unlit runtime materials). Baking on Windows should work meanwhile.
- 🟡 Light intensity tuning against the original look (compare screenshots in-game)
- ✅ Music (`.umx` Impulse Tracker modules → WAV via ffmpeg/libopenmpt, looping SoundWaves)
- ⬜ Skeletal meshes + animations (soldiers, first-person weapons): MeshAnimation parses; SkeletalMesh reader + PSK/glTF export
- ⬜ `DeviceMesh` / `LodMesh` (crates, pickups, effects)
- ⬜ Movers (doors/lifts are brushes with keyframes), water zones → volumes, ladders → ladder volumes
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
- ⬜ Weapon drops as pickups, death drops
- ✅ Deathmatch, Team Deathmatch
- ⬜ CTF, Holdout (Domination), Captains, Detonation, Safe Cracker, Trailer — data already in converted maps as `AMFLegacyActor`
- 🟡 Bots: MF roster (16 named bots, favourite loadouts), roam/aim/strafe/fire on navmesh (`?Bots=N?BotSkill=0.7`); needs nav data built + in-game testing
- ⬜ Vehicles: Buggy, Humma, Truck, APC (reverse `handling.hnd2` / `.COL`; Chaos Vehicles or a light custom raycast model)
- 🟡 Ladders (LadderClimber → `AMFLadderVolume`, untested in game); ⬜ swimming/drowning, water zones
- ⬜ Community mods: ZombieGame, KOH, TugGame, CoopMode, arenaMode … (source in MF-mods)

## Presentation
- 🟡 Canvas HUD (functional, plain shapes) → MF HUD textures / icons
- ⬜ First-person weapon models + animations, third-person soldiers, team skins
- ⬜ Effects: muzzle flash, tracers, impacts, explosions, blood (cheap sprite particles)
- ⬜ Menus: main menu, server browser (master server: 333networks protocol used by MF), options
- ⬜ Touch controls (mobile)

## Maps
- ✅ mf-warehouse converts and builds
- ⬜ All stock maps (airport, carpark, dockyard, ghetto, hydroworks, polar, prehistoric, rail quarry, sawmill, space station, toreford, tropics, waterfront, western …)
- ⬜ **mf-warehouse merge** with the Mobile Forces Source version (see [WarehouseMerge.md](WarehouseMerge.md))
