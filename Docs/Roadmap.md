# Roadmap

Legend: ✅ done · 🟡 partial · ⬜ todo

## Foundation
- ✅ UE 5.8 project: forward/mobile rendering, no post, static lighting, ~80 plugins off, Low scalability defaults
- ✅ Git repo, open-source layout (code/tools/docs tracked, original assets generated locally)
- ✅ Docs for the team (this folder)
- ✅ Licence: MPL-2.0 ([LICENSE.md](../LICENSE.md)); credits ([CREDITS.md](../CREDITS.md), in-game Credits page)
- ✅ Name: **Desktop Forces** (2026-10-20); the original game's converted content is the separate *Mobile Forces Classic* plugin, community maps whose authors agreed are part of the game ([packs](Pipeline.md#content-packs))
- 🟡 CI build (Linux + Windows): `.github/workflows/build.yml` on a self-hosted Windows runner, not run yet; ⬜ automated import test on a small map

## Pipeline (`Tools/mfport`)
- ✅ Package reader (v83), BSP world geometry, static meshes, actors, lights, textures, sounds
- ✅ UE importer commandlet, map builder, Lightmass bake step
- ✅ Override folders for clean replacement textures/sounds
- ✅ Lightmass on Linux (UE 5.8.3): 6-byte patch of the shipped library (once per machine) + `-messaging` for the bake commandlet ([Pipeline.md](Pipeline.md#baking-on-linux-ue-583))
- ✅ Lighting: UE1 light values (intensity ≈ π), sky windows transparent to light, 2 Lightmass bounces, soft light radii, small unshadowed floor from each zone's ambient (world vertex colour, `MPC_MF`) - all baked
- ✅ Music (`.umx` Impulse Tracker modules → WAV via ffmpeg/libopenmpt, looping SoundWaves)
- ✅ Skeletal meshes + animations: `mflib/skelmesh.py` (SkeletalMesh + MeshAnimation -> glTF, Interchange import; `mfport.py skeletal`, importer step `skeletal`): the soldiers and every first-person weapon (stock and mods)
- ✅ Rage `DeviceMesh` (vehicles, wheels, crates, safes, flag bases, det locks, holdout timers): `mfport.py props`
- ✅ `LodMesh` first frame (weapon carry models, rockets, grenades, det key); ⬜ Rage `CollisionMesh`, LodMesh animation
- ✅ Movers (`ADFMover`: doors / lifts from their brushes and keyframes), zones → volumes (`ADFZoneVolume`: water, pain, currents)
- ✅ Ladders → `ADFLadderVolume` with nav links
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
- ✅ Weapons: knife, pistol, machine gun, shotgun (one barrel or both; `?Boomstick`), sniper (charge + variable scope), rocket launcher, grenades (timed/impact), adrenaline, armour
- ✅ Heavy machine gun (deploy, spin-up), trip bombs (laser + proximity mine)
- ✅ Rocket lock-on (SmartRocket), tracers, shell cases
- ✅ Loadout crates/zones, carry blocks, refill on entering
- ✅ Weapon drops (throw / death) as ground items taken through the loadout (MF Loadout_Ground); loadout opens anywhere
- ✅ Deathmatch, Team Deathmatch
- ✅ Holdout (Domination): `AHoldout` + `AHoldoutPoint`, captures, hold-time scoring, HUD readout, bot attack/guard goals
- ✅ Safecracker (two parts, safe cracking, gold), CTF, Detonation (`AFlag` shared), Captains
- ✅ Trailer: cab + hitched bomb trailer, delivery bays, countdown, detonation
- ✅ Chat (T/Y, Say/TeamSay), team orders menu (V, keys or mouse) with bot orders, MF message list
- ✅ N teams (`UDFGameSettings::Teams`, `?Teams=`), UE match states, `ULocalMessage` announcements
- 🟡 Bots: travel the whole map now (the warehouse navmesh is fine; a 30 s travel budget made them give up halfway), unstick by jumping, direct approach to goals off the navmesh edge. Ladder links still unverified. Roster (32 named bots in config, numbered extras beyond), loadouts, aim/strafe/fire, objective goals (`?Bots=N?BotSkill=0.7`). `DFNavTest` / `DFNavPath X Y Z` print reachability and routes. Never rebuild the navmesh in the light bake (it came out empty).
- 🟡 Vehicles: Buggy, Humvee, Truck, Armadillo APC as physics boxes with raycast wheels tuned from `handling.hnd2` / `.COL` (springs scaled to MF gravity), MF meshes, generators, enter/exit (E), run-over, explosives-only damage. ✅ passenger seats (shooting from them), Armed APC turret, wheel blow-outs, petrol caps, bots driving (MF road graph) / riding; ⬜ bots in boats, DeBall bot drivers
- ✅ Ladders: grab when close / falling in, climb down from the top, floor walks again; ✅ swimming / drowning, water / pain zones
- ✅ Map actors: teleporters, placed items (respawning), periodic sounds, movers (scenery, spinners, bump doors), invisible walls, spawn room doors, coop maps' placed RageBots, particle effects (ParticleEffect / FireyExample: fires, smoke, sparks, rain); ⬜ trigger / special event chains (Treasure_Finders lighthouse, bells), LaserBeam
- ✅ MF-mods, part of the game (`Game/`, `Weapons/`, `Vehicles/` subfolders, [Modding.md](Modding.md)): Molotov, KOH, Totem, TripLasers, SeekerMines, SentryGun, TugGame, CoopMode, ZombieGame, deagle, grenLaunch, PredatorMissile, ArmedAPC, arenaMode, DeBall, battleRoyale, TurboCars, boat, jeep, mutPack changeDamage
- ⬜ Mods for the packaged game: a `Mods/` folder of paks scanned at start, mod maps in the menu
- ✅ More MF-mods: heli + heliHmg, piper / piperTs (plane), vote, mutPack replaceVehicles / addVehicles; jeep gun model (UE1 vertex Mesh reader)
- ✅ server weapon mods (AK-74, M4A1, FAMAS, G36, SC-20K, Ultimax, MAC-10, Glock, Desert Eagle, M24, WA 2000, M1887, M18 smoke; Unverified pack), parachutes; improvedRL (Gian: `?ImprovedRL=1`), OPWeapons (`?OPWeapons=1`), BoxPhysics (`WoodenBox`), Desert Eagle moved to core (2026-10-22); FNG's server mutators as options (map rotation, server messages, NoWeapons, AllVehicles, LowGrav; [Hosting.md](Hosting.md)); ⬜ player skins (Serious, SwatSkin: unverified)
- ✅ Mine generator runtime and map-marker hookup (`World/MineGenerator.cpp`, `DFGameMode::HandleMatchHasStarted`): proximity mines, five-second rearm, vehicle triggers and splash-only river generators. Converted mf-Pobojowisko_VerII contains the markers. Runtime/marker conversion is regression-tested; full map playtesting remains separate.
- ✅ Water (swimming, drowning, pain / heal zones, currents, floating vehicles)

## Presentation
- ✅ MF HUD with the original RageHUD art (blue / red team sets, box segments per carry block, ammo bars, health/armour silhouettes, loadout icon, loadout emblem backdrop, HUD sizes 0-4); plain fallback without the art; MF bitmap fonts, pain flash, translucent crosshair, team score panels, Safecracker bars, minimap (MF MiniMapInfo texture, team border, team-mates, objectives); ✅ scoreboard
- ✅ MF loadout behaviour: frozen soldier (the view still turns), visible to all, one cursor through store and carried items (put back from the inventory), W/S/wheel/Fire/E, number rows + X, sounds, spawn protection
- ✅ MF console commands and cheats ([Commands.md](Commands.md)), KillZ under every map
- ✅ Generated art of our own (`Tools/artgen`): loadout crates with team stencil
- ✅ Graphics options above potato via scalability levels (MSAA, anisotropy, Epic bloom); texture quality Low samples 256 px
- ✅ First-person weapons: MF's arms models (PlayerViewMesh) with their select / fire / reload / zoom animations, MF PlayerViewScale / PlayerViewOffset; ⬜ per-weapon extras (shotgun barrel-state animations, grenade pin pull wind-up, knife catch; HMG deploy done), weapon bob
- ✅ Third-person soldiers: MF helmet / balaclava models, team and captain skins, MF's animation choice (Pawn.uc Get*Anim: weapon sets, zoomed poses, strafes, crouch, swim, jump / land, throws, deaths), carry meshes in hand, drivers and seated passengers; ✅ saved player model and preferred team choice; ⬜ ragdolls
- 🟡 Effects (MF RageSprite particles → Cascade, `mfport.py effects`): explosions (grenades, rockets, trip bombs, mods), vehicle explosions with burning debris and ground fires, tyre smoke, damage smoke, rocket/Predator fire trails, wall hits, blood, map emitters; ✅ tracers, shell cases, bullet holes and scorch marks (decals), smoke grenades; ⬜ muzzle flashes, water rings, bubbles
- 🟡 Menus: title screen over the MFS pictures with the start-up theme, play / join / options (render scale, brightness, mouse, FOV …) / credits / quit, Esc menu in game; ⬜ server browser (333networks master server protocol)
- ⬜ Touch controls (mobile): plan under "Open threads"

## Maps
- ✅ mf-warehouse converts and builds
- 🟡 More maps convert and bake: Carpark, Toreford, MF-Filip, mf-Microwarehouse, Mf-MicroBunker, mf-MicroTower. ⬜ the rest (airport, dockyard, ghetto, hydroworks, polar, prehistoric, rail quarry, sawmill, space station, tropics, waterfront, western …)
- ✅ mf-warehouse (original) is the map to play and build on
- ⬜ Add Mobile Forces Source features onto it by hand (secrets, `func_breakable` doors and passages, props) — the automatic geometry merge was tried and dropped: seams, holes and misaligned pieces ([WarehouseMerge.md](WarehouseMerge.md); the alignment and prop converter stay useful for placing MFS pieces)

## Options and tooling follow-up
- ✅ One Python build/play launcher; platform packagers named `package.sh` / `package.ps1`.
- ✅ Commit-preserving public sync without checking out the asset branch; tested conflict stops.
- ✅ Advanced FPS/VSync/render-scale controls, platform-aware display modes, crosshair size.
- ✅ One `ScoreLimit` rule; parachute server option, jump-deployment guard and vehicle closure.
- ✅ UE 5.8.4 Linux Development and Shipping compile; native options/parachute regression
  passes; a separate client receives the server's score limit and parachute rule.
- 🟡 Windows runtime, physical fullscreen switching/VSync, and multiplayer visual presentation
  still need hands-on validation. Linux offscreen tests do not cover those paths.

## Options follow-up (2026-10-27)
- Menu defaults now use factory values; active tab and Advanced expansion persist.
- Console checkbox follows viewport creation; crosshair visible bounds normalized; mouse settings moved to Controls.
- Linux display-mode detection uses ApplicationCore; window resolution is checked against the actual viewport.
- Effects and Announcer audio controls; Full scorch marks conform to surfaces without DBuffer.
- Humvee: MF front-seat driver preference, rear-seat proximity, and explicit closest-seat entry; parachute threshold raised by 25 cm/s.
- Platform paths in the example TOML live only under `[linux]` and `[windows]`.
- Validation results and remaining platform limits are recorded in the dated tests report.

## Vehicle and effect follow-up (2026-10-27)
- Manual entry uses nearby seats with MF's front-seat driver preference; Right Alt uses the closest free seat, including the driver.
- Suspension support follows the ground normal. Wheel queries block soldiers, while pawn
  contacts no longer act as immovable physics obstacles to the chassis.
- Vehicle health controls smoke density and opacity. Rocket and Predator flight effects
  reuse converted fire and smoke flipbooks with bounded emission rates.
- Texture conversion resolves the complete palette path. BloodSpang.Palette25 previously
  loaded the unrelated root Palette25, losing both red colour and graded alpha.
- Runtime measurements and platform limits are recorded in the dated tests report.

## Gameplay lifecycle follow-up

- Predator flight effects stop on replicated explosion and destruction. Rocket trails and
  vehicle smoke also stop on EndPlay, including direct removal and timeouts.
- Vehicle exits restore position and possession before collision; dead pawns never resume
  falling movement. Exiting occupants get one second of immunity to that vehicle only.
- Front-seat driver preference follows MF's near-driver rule. Right Alt / UseClosest
  selects any closest free seat, including an interior passenger seat with no driver.
- Empty moved/damaged generated vehicles recycle; drivers remain weapon-query targets.
- Bot reloads reset held-fire bookkeeping, verified beyond the first magazine.
- Feign death uses a replicated, damageable stance and existing MF DeathEnd animation;
  Left Alt toggles it, with a rebindable Controls entry.

## Vehicle and content polish follow-up

- Closest-seat Use is a remappable action on Right Alt; normal Use keeps MF front-seat priority.
- Physical rollover recovery, reduced traction-induced roll, truck turbo, accelerating expiry
  pulses, short-lived expiry debris, and two-sided original team-emblem flags.
- Soft crossed-quad tracers and live owned-ammo counts in the classic loadout.
- Space-station double-sided grating collision refreshed without rebuilding its lighting.
- Saved model/team preferences; team balance remains authoritative.
- Remaining playable maps have a resumable conversion/import/lighting queue. Completion
  is recorded in `Tools/mfport/work/remaining/progress.json`; queued does not mean finished.

## Open threads (handoff, 2026-10-28)

Each item is sized for one focused conversation. Read `AGENTS.md`, then the linked doc.

1. **Content packs as separate paks** ([Pipeline](Pipeline.md#content-packs)). A packaged build
   is one `pakchunk0` because chunking is off. Plan:
   - Packs: core (our code and art, `/Game/MF` minus Community), community (`/Game/Maps/<agreed
     maps>`, `/Game/MF/Community`), unverified (`/MobileForcesUnverified`), classic
     (`/MobileForcesClassic`, local builds only).
   - The importer writes one `PrimaryAssetLabel` per pack root (`bLabelAssetsInMyDirectory`,
     `CookRule=AlwaysCook`, ChunkId core 0 / community 1 / unverified 2 / classic 3; priority
     decides which chunk a shared asset lands in). Then drop the pack's `DirectoriesToAlwaysCook`
     entry and set `bGenerateChunks=True` under `[/Script/UnrealEd.ProjectPackagingSettings]`.
   - The output is `pakchunkN-<Platform>.pak/.utoc/.ucas`. UE mounts every pak in
     `Content/Paks` at start, so adding or removing a pack's files adds or removes the pack.
   - Runtime: the cooked asset registry lists every chunk's maps, so `DF::FindMaps` must also
     skip packages whose pak is absent (`FPackageName::DoesPackageExist`). Plugins stay enabled,
     so `IsValidPath` alone no longer tells.
   - Community maps that still borrow retail textures (`mfport.py packs`) need the classic pak
     until their replacements exist.
   - Test: cook, delete one chunk, check the map list and that a community map loads.
   - Mods made after release are a separate step: DLC cooks (`-DLCName`,
     `-BasedOnReleaseVersion`) and a `Mods/` folder ([Modding](Modding.md#content-after-release)).
2. ✅ **First-person rig accuracy** (2026-10-28): RotOrigin applied as UE1 does after the .psk Y
   flip (the pitch cants the gun about its barrel instead of raising it), MF's weapon roll ×
   Hand (`ArmsRoll`), and CalcDrawOffset's 0.9 / FOV offset scale.
3. **Audit ported defaults for inherited values**: decompiled defaults omit what equals the
   parent's value (see AGENTS.md). PlayerViewOffset is fixed (f505a4d, 6b67cb3); re-check other
   ported struct and plain defaults with a parent-chain helper in `mflib`.
4. **Developer console in Shipping**: the option is built and compiles in Shipping. Opening it
   in a packaged Windows build is still unchecked.
5. **Android: first run on a phone.** Done (2026-10-28):
   - The SDK is installed per [Building](Building.md#android).
   - `Tools/package.sh Android Development --maps ...` builds a 443 MB arm64 APK.
   - Touch controls: virtual sticks and an action-button overlay, tested on desktop.

   Next:
   - Install it on a device and check rendering (ES3.1 / Vulkan), touch feel and performance.
   - Size the buttons by screen DPI, and add tap support to the loadout and menus.
   - Cook per pack (thread 1) so the APK doesn't carry every classic asset.
6. **Bots and vehicles**: bots see drivers and follow on foot when their order-giver drives.
   Riding along and using turrets is still untested and partly unhandled.
