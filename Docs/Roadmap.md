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
- ✅ Lightmass on Linux 5.8.4: hash-guarded `mfport.py repair-lightmass`, original library backup, and `-messaging` for bakes ([Pipeline.md](Pipeline.md#baking-on-linux-ue-584)). The 5.8.3 byte offset differs.
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
- ✅ server weapon mods (AK-74, M4A1, FAMAS, G36, SC-20K, Ultimax, MAC-10, Glock, Desert Eagle, M24, WA 2000, M1887, M18 smoke; community pack since 2026-10-29), parachutes; improvedRL (Gian: `?ImprovedRL=1`), OPWeapons (`?OPWeapons=1`), BoxPhysics (`WoodenBox`), Desert Eagle moved to core (2026-10-22); FNG's server mutators as options (map rotation, server messages, NoWeapons, AllVehicles, LowGrav; [Hosting.md](Hosting.md)); ⬜ player skins (Serious, SwatSkin: unverified)
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
- ✅ One Python build/play launcher (`play.py`) and one packager (`package.py`, 2026-10-29; was `package.sh` / `package.ps1`).
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
- Remaining playable maps have a resumable conversion/import/lighting queue
  (`mfport.py remaining`, progress in `Tools/mfport/work/remaining/progress.json`). All 55 were
  converted, imported and baked on 2026-10-29; mf-nc, MB-ShippingArena and MB-Ballpark load
  with bots without errors. The rest still need a play pass.

## Open threads (handoff, 2026-10-28)

Each item is sized for one focused conversation. Read `AGENTS.md`, then the linked doc.

1. ✅ **Content packs as separate paks** (2026-10-28): one pak per pack, labelled by the importer,
   dropped in or out of `Content/Paks` ([Building](Building.md#package)). Tested on Linux:
   without the classic pak, community maps still load and classic maps leave the map list.
   Still open:
   - DLC-style mod paks made after release (`-DLCName`, a `Mods/` folder;
     [Modding](Modding.md#content-after-release)).
   - Android: game data now in an OBB beside the APK (2026-10-29), every pack still inside;
     downloads per pack are thread 7.
2. ✅ **First-person rig accuracy** (2026-10-28): RotOrigin applied as UE1 does after the .psk Y
   flip (the pitch cants the gun about its barrel instead of raising it), MF's weapon roll ×
   Hand (`ArmsRoll`), and CalcDrawOffset's 0.9 / FOV offset scale.
3. **Audit ported defaults for inherited values**: `mfport.py defaults <Class> [Prop...]` prints the
   effective values down the parent chain (2026-10-28). Weapon audit: view offset, scale and roll
   all match. AIRating differs (ours / MF): knife 0.5 / 0.1, Deagle 0.5 / 0.45, grenade launcher
   0.55 / 0.45, Molotov, seeker mines and trip lasers 0.5 / 0.4. Check bot weapon choice before
   adopting MF's values; other properties are unaudited.
4. **Developer console in Shipping**: the option is built and compiles in Shipping. Opening it
   in a packaged Windows build is still unchecked.
5. **Android: first runs on a Pixel 9a** (2026-10-28, wireless adb).
   Works:
   - Menu (moved clear of the camera hole and enlarged on touch screens), and starting a match.
   - Tapping loadout items and the LOADOUT button.
   - Both sticks, FIRE (a bot kill).
   - 30 FPS cap: game thread 15 ms, draw 12 ms, GPU 6.5 ms, about 850 MB in mf-warehouse
     with 6 bots.

   Proposals:
   - The HUD canvas (loadout list, minimap, kill messages) ignores the safe zone and sits under
     the camera hole in landscape. Offset it by the safe-zone padding. HUD text is 1.5× on touch
     screens (10c57c2, 4757ce0; checked on the 9a).
   - Offer 60 FPS: the 9a has roughly half its 33 ms frame budget to spare.
   - Button icons instead of words, a layout editor (MF-style), touch-look sensitivity, and
     tap-to-fire on the right half as an option.
   - Options pages (sliders, combos) still need checking with fingers.
   - Since 2026-10-29 the data is in an OBB: a 103 MB APK and a 1.39 GB OBB for two maps
     (classic 1.22 GB of it). It runs from the OBB on the 9a. Install with the generated
     `Install_DesktopForces-arm64.sh` ([Building](Building.md#android)).
6. **Bots and vehicles** (2026-10-29):
   - Fixed: covering bots no longer pick their own vehicles, drop a vehicle they were walking to
     when given an order, and drive to the leader if already driving (1cbb8ac).
   - Untested: a full ride-along. On mf-Tropics the bots headed for the player's Humvee, but
     navmesh gaps and the fighting got in the way.
   - Test recipe: `-LogCmds="LogDF Verbose"`; DFTeleportNear Buggy 150 (DFEnterSeat needs to be
     right next to a vehicle; don't `DFLoadout` on maps that don't start in it); DFOrder 3 after
     the bots spawn; KillEnemy every few seconds.
   - Turret seats are not looked at yet.
7. ✅ **Downloadable content packs** (2026-10-29): `Tools/package.py <Platform> --downloads` puts
   only the core pack in the build (Android: one ~150 MB APK, data inside, instead of an APK plus
   a 1.34 GB OBB with three maps); community and unverified go to `Downloads/<Platform>/<content build
   id>/` for `https://mf.nofisto.com/download/` (Options > Game can point elsewhere), classic to
   `Sideload/` ([Building](Building.md#downloadable-packs)). Menu > Downloads lists the
   packs (Download / Cancel / Delete, progress); a finished pack mounts at once and its maps join
   the list; installed packs mount at start (`Core/DFDownloads`). Tested on the Pixel 9a against a
   LAN `http.server`: download, play mf-Tropics from the downloaded pak, restart, delete, kill
   mid-download and resume.
   - Found on the way: maps claimed what they use, so ~300 classic textures and sounds borrowed
     by community maps were cooked into the community pak too (Map type now non-recursive), and
     the three community labels shared one name (only one counted). Classic stays off the server:
     the packager checks the cooker's chunk lists.
   - Stock ChunkDownloader quirks worked around: it would mount `.utoc`/`.ucas` as paks (we mount
     the `.pak`), holds a whole file in memory (64 MB container parts on Android), and records
     finished files only when a chunk download starts (we write its `LocalManifest.txt`).
   - Cooking is deterministic: unchanged packs keep their SHA1 between builds (players don't
     download them again; the server can hardlink them).
   - Still open: upload a build to `https://mf.nofisto.com/download/` and test over HTTPS;
     the Windows packaging path only dry-run on Linux so far; old build folders on the server need
     keeping while those builds are around.
8. ✅ **Packs reshuffled** (2026-10-29): classic is the retail game only (+ `[classic]`: custom
   maps' music, often taken from other games); `packs.toml`'s "core" is now "community" (core =
   the game's own pak); the servers' weapon mods, the MF-mods vehicles / Desert Eagle / grenade
   launcher art and the community maps' own sound packages moved to community; the 51 custom maps
   by authors not asked yet and their packages to unverified (a public download now). Done in
   place by `mfport.py import --steps repack` (no re-bake).
9. **Replacement art for borrowed classic assets** (user idea, 2026-10-29): start with the
   [replacement art handoff](ReplacementArtHandoff.md) for candidates, rejected mappings and next steps. Community maps still
   use ~300 classic packages (textures in `aq`, `Mio`, `rage_*`, `car_park`, `Dam` ...; sounds in
   `levelSFX`, `TropicsSFX`, `Aquarius` ...), so without the classic pack they show missing
   textures and play no ambient sounds (mf-Tropics on the 9a). `mfport.py packs` lists them;
   `Tools/artgen/lookalike.py` overrides can replace the textures in the community pack (or
   another generator); sounds need free/CC0 or team-made replacements.
   - Selected Starter/Fab image staging is available through `mfport.py replacements`:
     local-only provenance, maximum 512 px, and rejection of opaque replacements for holes.
     The Starter poured-concrete sample passed staging but was rejected after an in-scene
     comparison: it removes this target's slab pattern. Its material change and asset were
     dropped; seek a slab/tile replacement. See [systems follow-up](Performance.md#systems-follow-up-2026-10-29).
10. **Flat crate in mf-warehouse** (Filip's phone screenshot `Screenshot_20261028-224646.webp`,
    not in the repo: it shows classic textures): outdoors under the sunset sky,
    a light wooden crate stacked on a dark one looks like a single face. Warehouse crates are BSP
    geometry (`rage_warehouse.wood.ware_wood_*`), so likely a brush that lost faces in conversion
    (or a one-sided face). Not in the red loadout room (11100, 8600) or the red trailer yard
    (9600, 3600). 2026-10-30: Filip no longer sees it on desktop; check the phone (mobile
    renderer / 256 px textures) before closing. Same map: the floor grate in front of the red
    base's Armadillo garage (3967, 3825, -216; `ware_metal_37`, thin bright bars on black)
    shimmers when moving. One face, no z-fighting; trilinear mips made no visible difference
    (the bars draw from the top mip there). Candidates: a softer mip / LOD bias for that
    texture, or AA.

11. **Systems performance follow-up** (2026-10-30): StateTree dependencies removed after
    an asset-reference audit; editor build and six gameplay suites pass. Bot decisions remain
    at 5 Hz, with staggered initial timers reducing measured 48-bot timing spikes. Animation
    budgeting was tried and removed (no frame-time gain). MF_TestRange was regenerated and
    rebaked after its old resave lost the gameplay actors. A regression test now verifies four
    neutral starts, two per team, team selection and navigation coverage. VehicleProject builds
    and runs at 30/60/120 FPS; DF recovery/turbo checks pass at all three caps. No matched test
    establishes a reason to migrate vehicle physics. Comparison and replacement-art results are in
    [Performance](Performance.md#systems-follow-up-2026-10-29). Spatial prop instancing remains
    a candidate, pending a measured prototype and rebake.
12. **Optional dynamic lighting for strong PCs: Lumen / "Lumen Lite"** (Filip, 2026-10-29). Today
    every map is baked (Lightmass) and Lumen is off at project level
    (`r.DynamicGlobalIlluminationMethod=0`, `r.ReflectionMethod=0` in `DefaultEngine.ini`).
    With it on, the scalability groups gate Lumen per player setting (5.8.4
    `BaseScalability.ini`): GI Low = Lumen off, GI Medium = irradiance-field gather
    (`r.Lumen.FinalGatherMethod=0`, UE 5.8's Beta "Lumen Lite": ~2x cheaper than full Lumen, the
    default on handheld consoles), High/Epic = full Lumen; Lumen reflections only from High.
    Before offering it as an option:
    - Lumen needs the deferred renderer; PC runs forward shading (`r.ForwardShading=True`): a
      renderer choice for strong PCs (needs a restart), phones stay mobile / baked;
    - Epic's docs: with Lumen enabled, precomputed lighting is disabled and lightmaps hidden, and
      Static lights (all ours) add nothing. Test whether lightmaps come back where scalability
      turns Lumen off (GI Low), else Low loses its bake; maps would need Stationary/Movable
      lights for Lumen;
    - measure mf-warehouse low end / high end against the baked look.
13. **Server list: LAN and internet through the 333networks master** (Filip, 2026-10-30). Filip
    runs a 333networks master at `master.nofisto.com` (TCP 28900 answers `\basic\\secure\<challenge>`,
    web list and `/json/` on https). Use its GameSpy-v1 protocol rather than an online subsystem:
    plain UDP/TCP through UE's `Sockets` module, next to the game's NetDriver, nothing in UE
    against it. Plan:
    - register a new gamename (`desktopforces`, not MF's `mobileforces`: old UE1 clients would try
      to join) with its cipher in the master's game table;
    - servers (dedicated and listen, a `UGameInstanceSubsystem` or the game mode): a UDP query port
      (game port + 1, 7778, like UE1) answering `\basic\` `\info\` `\rules\` `\players\` `\status\`
      `\echo\`, and `\secure\` with `\validate\` (gsmsalg + cipher); `\heartbeat\<queryport>\gamename\desktopforces`
      to the master's UDP port every few minutes and on state changes (`\statechanged\`). Master
      list in `DefaultGame.ini`, an `-nomaster` / `?Public=0` switch for private servers;
    - client: TCP list request (`\gamename\..\location\0\validate\..\final\` then
      `\list\\gamename\desktopforces\final\`), then query each server itself over UDP for name,
      map, mode, players and ping (as UE1 did); a LAN tab broadcasts the same `\status\` query to
      the query ports on the local subnet, so one protocol covers both;
    - menu: a server browser page in Join (sort by ping / players, filter mode / map, the pack a
      map needs from Downloads), refresh, double click joins;
    - hosts forward UDP 7777 and 7778 (no NAT traversal in this protocol); IPv4 only; test with a
      local master first (333networks MasterServer runs anywhere), then `master.nofisto.com`.
14. **Dedicated server builds through Epic's container image** (Filip, 2026-10-30). Server targets
    need an engine built from source; the installed 5.8.4 can't build them. Epic publishes
    source-built engines as images at `ghcr.io/epicgames/unreal-engine` (needs the Epic account
    linked to GitHub, then `docker login ghcr.io` with a GitHub token with `read:packages`).
    Docker 29 is installed here, 90 GB free on `/`. Plan:
    - check the tag list for `dev-slim-5.8.4` (the server must be the clients' engine version:
      the network check includes the engine's compatible changelist); a third-party mirror lists
      `dev-slim-5.8.0` / `5.8.1`, a community page puts dev-slim at ~35 GB (both unconfirmed);
    - add `Source/DesktopForcesServer.Target.cs` (`Type = TargetType.Server`) and a `package.py
      Linux --server` mode that runs BuildCookRun inside the container (project mounted, `-server
      -serverplatform=Linux -noclient`), the same packs as the client build;
    - test: server on the PC, desktop and phone clients joining; then a small VPS. The dev images
      may not be redistributed (Unreal EULA): build with them, ship only the server build.
    Without it, servers are listen servers (`?listen -nullrhi -nosound` on a packaged game).
