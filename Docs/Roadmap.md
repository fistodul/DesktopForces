# Roadmap

Legend: ✅ done · 🟡 partial · ⬜ todo · ⏸ measured, held back

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
- ✅ Minimap textures (MiniMapInfo): MF tiles them (NumTexturesWide × High, 255 px each, 28 maps), stitched into one by `mflib/minimap.py` (only the first tile used to be stretched over the map); maps without a MiniMapInfo (56, VMF ones too) get one drawn from their floors (lighter higher up, lines at steps)
- ✅ Textures straight from the packages: S3TC copies (MF's D3D look), palette alpha for HUD art
- ⬜ Mounted gun aim: the crosshair marks where the gun points, which trails the view while the turret turns; make the gun converge on the view's aim point
- ✅ Phone frame rates (2026-11-08, Pixel 9a): the list is the rates that divide one of the panel's refresh rates (60 / 120 Hz: 120, 60, 40, 30, 24), and the cap goes to the frame pacer too (`FPlatformRHIFramePacer::SetFramePace`: Swappy picks the panel mode; `t.MaxFPS` alone left the phone at 60). The cap steps down when the frame rate stays under 90 % of it for 30 s (logged, this session only). Measured 59.9 FPS at 60 in Mental Race with 4 bots. That 9a has Smooth Display off (`peak_refresh_rate` 60), so Android holds 60 Hz whatever the game asks: a 120 cap stepped down to 60 after 30 s
- ⬜ Kill credit left over from the code review: predator missile, sentry and seeker mines still read the owner's controller when they hit, and a gunner's or driver's shots too; projectiles, trip bombs and molotovs keep it from launch
- ⬜ Hand-off 2026-11-03, open after the last feedback round:
  - Not tried in game yet: trip laser on vehicles, exiting a rolled vehicle (view level), rocket APC crosshair, rotor start, flags on Armadillo, Jeep and Heli
  - Review not done for `DFMenu`, `DFHUD`, `DFCheatManager`, the download code, most of Predator and Totem
  - Beacon keeps announcing if a host joins another server directly (`Stop` only runs on a standalone map)
  - `mf-warehouse` explosive table: rocket 202 at the centre (135 x 1.5 hardcore), 100 at about 4.5 m, 8 at the 9 m edge; `RocketDamageScaleOnFoot` is mutPack's optional x2
- ⬜ Hand-off 2026-11-04, after the play-test round:
  - Reference video: `python3 Tools/mfvideos.py --work <dir> scan|report|download|frames` lists the channel (335 videos, 319 on 57 of our maps), kept the newest whole video per map (56, 7.3 GB, 720p, no sound; deleted 2026-11-05 after taking stills every 30 s into `frames/<map>`, `download` fetches them again, `frames` can also read straight from YouTube). Prefer the newest as the truth; older ones may show since-changed play
  - To compare with the videos (no changes without asking): lighting per map and area (MB-DesertArena's spotted half), vehicle smoke, vehicle expiry tint (keep DF's), ladder grab distance (we pull you onto the rungs at radius + capsule radius), climbing
  - Open: warehouse exterior stairs snag (map geometry, not a stair mesh: find with `DFTraceMat`), buggy wheels clip into walls and the car can wedge upright (exit is safe now), brief white text top-left during play (not in the recordings or the HUD message log: needs a screenshot), bullets push the buggy (about 1.8 m under 4 s of MG) but whether heavy vehicles should move as much as in MF is unmeasured
  - Not tried by a player: M1887 quick alt shot (0.88 s), three sentry guns, Join page cancel, grates with alpha to coverage
- ⏸ Texture arrays per map to cut draw calls: tried on mf-The_Bank and mf-nc, no measured gain, kept off ([Performance](Performance.md#arrays-and-streaming-measured-2026-11-01)); phone not tested
- ✅ Full rebake of all maps (2026-11-04/05, `mfport.py rebake`, resumable from work/rebake/progress.json): suns 46° too steep, overlapping lightmap UVs, 256 px static mesh lightmap cap, two-sided static mesh collision, instanced solid props (their bounds are refreshed when a lighting build starts: DesktopForcesEditor.cpp), stitched/generated minimaps. 87/87 lit. Found on the way: "Failed to connect to Swarm" is transient (rerun); mfs-warehouse's 170-nines `_distance` crashed Lightmass (radius capped at 80 m); the navmesh tile pool dropped floors on mf-Jungle and the warehouse maps (`AverageLayersPerTile=7`); bare `LoadoutCrate`s were drawn solid and lifted spawns off the navmesh (now hidden, 7 maps rebuilt). Still open from that list (water sheets let light through since that rebake): comparing lighting per map against the videos (waterfront reads too bright), HMG and jeep gun animations, MAC-10 hand speed, steering
- ⬜ Share replacement art between packs: classic maps could use the replacement textures the community maps already ship wherever the original shows no detail or text the replacement lacks (signs, logos, numbers stay). One copy instead of the S3TC original + its replacement saves disk and download size; needs a per-texture check (side by side, maybe a perceptual diff) and a mapping table in `packs.toml` or the replacements manifest
- ⏸ Zone-based level streaming / HLOD for big outdoor maps: held back, same section
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
- 🟡 Vehicles: Buggy, Humvee, Truck, Armadillo APC as physics boxes with raycast wheels tuned from `handling.hnd2` / `.COL` (springs scaled to MF gravity), MF meshes, generators, enter/exit (E), run-over, explosives-only damage. ✅ passenger seats (shooting from them), Armed APC turret, wheel blow-outs, petrol caps, bots driving (MF road graph) / riding; ⬜ bots in boats, MoBall bot drivers
- ✅ Ladders: grab when close / falling in, climb down from the top, floor walks again; ✅ swimming / drowning, water / pain zones
- ✅ Map actors: teleporters, placed items (respawning), periodic sounds, movers (scenery, spinners, bump doors), invisible walls, spawn room doors, coop maps' placed RageBots, particle effects (ParticleEffect / FireyExample: fires, smoke, sparks, rain); ⬜ trigger / special event chains (Treasure_Finders lighthouse, bells), LaserBeam
- ✅ MF-mods, part of the game (`Game/`, `Weapons/`, `Vehicles/` subfolders, [Modding.md](Modding.md)): Molotov, KOH, Totem, TripLasers, SeekerMines, SentryGun, TugGame, CoopMode, ZombieGame, deagle, grenLaunch, PredatorMissile, ArmedAPC, arenaMode, MoBall, battleRoyale, TurboCars, boat, jeep, mutPack changeDamage
- ⬜ New modes to consider (reminders, nothing designed yet):
  - Bomb defuse: attackers carry one bomb to a site and plant it, defenders defuse it, in rounds without respawning. Mostly built from what we have: sites = the maps' `AHoldoutPoint`s (KOH / Holdout / Arena placement; maps with fewer than two get spots added like other mode spots), round flow and "stand on it for N s" = Arena (`Game/Arena`), the bomb = Detonation's carried key (`AFlag`, drop on death, return when untouched) and its countdown / blast; new: plant and defuse as interrupted timed actions, which side attacks swapping each half
  - Sky surfing: MFS `cfg/gamemode/surf.cfg` (`sv_airaccelerate 200`): Source-style air strafing on slanted surfaces; needs air control in `UDFCharacterMovement` and surf maps
  - Injustice (MFS `mp_injustice`, `cfg/gamemode/injustice.cfg`): meant as blue players helping the map's NPCs fight a few red ones (one red per nine blue in `hl2mp_gamerules.cpp`; blue support kit: medkit, physcannon, bugbait; red attack kit, 1 hp/s regeneration, own `info_player_god` spawns; NPC sides in `InitInjusticeAIRelationships`). DF has no NPCs, so a simpler take: the red are Zombie mode's NEMESIS in all but name (lone side scaled by the ratio, regeneration, harder hits), so build it as a ZombieGame preset rather than a new mode: `?Infect=0`, red nemesis-boosted from the start at a set ratio (MFS 1 : 9) instead of only when alone against 5+, red keep ranged weapons (`?ZombieWeapons`), human bodies, blue topped up with bots in place of the NPCs. Unlike a nemesis round (dead humans wait for the next round, the nemesis respawns) blue respawn, so the round needs a cost: lives on both sides (blue a shared pool, about 2 x their count, or waves every 15 s; red about 3 each; out of lives loses, at the clock blue win or the larger share of lives left), or an objective (blue hold a Holdout point red must break). Balance to be tried in play
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
   - ✅ (2026-11-08: the HUD starts ~156 px in from the cutout side: loadout list, minimap, health, messages) The HUD canvas (loadout list, minimap, kill messages) ignores the safe zone and sits under
     the camera hole in landscape. Offset it by the safe-zone padding. HUD text is 1.5× on touch
     screens (10c57c2, 4757ce0; checked on the 9a).
   - ✅ 60 FPS offered and held (59.9 FPS in Mental Race with 4 bots, 2026-11-08).
   - Button icons instead of words, a layout editor (MF-style), touch-look sensitivity, and
     tap-to-fire on the right half as an option.
   - ✅ Options by touch (2026-11-08): Video page, Advanced Graphics, the FPS combo and Apply all work by tapping; the game type list shows the new modes.
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
     dropped. See [systems follow-up](Performance.md#systems-follow-up-2026-10-29).
   - 2026-10-30: all 73 non-classic maps run with Classic unmounted: generated textures, particle
     flipbooks and ambience, swapped in place by the importer's `repoint` step. Only their music
     has no stand-in. Gameplay assets (soldiers, weapons, pickups, vehicles, HUD) still need
     Classic. Not yet cooked or listened to; limits are in the handoff.
10. ✅ **Flat crate in mf-warehouse** (Filip 2026-11-08: fixed already) (Filip's phone screenshot `Screenshot_20261028-224646.webp`,
    not in the repo: it shows classic textures): outdoors under the sunset sky,
    a light wooden crate stacked on a dark one looks like a single face. Warehouse crates are BSP
    geometry (`rage_warehouse.wood.ware_wood_*`), so likely a brush that lost faces in conversion
    (or a one-sided face). Not in the red loadout room (11100, 8600) or the red trailer yard
    (9600, 3600). 2026-11-08: phone and desktop shots from the same cameras match where I looked
    (indoors near (12100-14500, 4300-6900)); the crate itself not found yet: needs the spot.
    2026-10-30: Filip no longer sees it on desktop; check the phone (mobile
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
    - menu (done 2026-11-01 without sorting choice, filters or LAN: fixed ping order): a server browser page in Join (sort by ping / players, filter mode / map, the pack a
      map needs from Downloads), refresh, double click joins;
    - hosts forward UDP 7777 and 7778 (no NAT traversal in this protocol); IPv4 only; test with a
      local master first (333networks MasterServer runs anywhere), then `master.nofisto.com`.
14. ✅ **Dedicated server builds** (2026-11-06): `python3 Tools/package.py Linux --server` builds
    and cooks `DesktopForcesServer` inside Epic's engine image (the Launcher engine has no server
    targets) from a copy of the project, and packs a tarball that runs on any x86-64 Linux with
    glibc 2.28+, no Docker or engine there ([Hosting](Hosting.md#ways-to-run-one)). Tested: unpacked
    in a fresh folder, run natively, an editor client of the same version joined it. 2026-11-08:
    the full-map build after the rebake (`20261108`, every mode incl. Knife Fight / Mental Race),
    the Pixel 9a joined it (Knife Fight, bots) and an editor client (Mental Race). Still open: a
    VPS run and Windows clients against it.
15. ✅ **Prop lightmap UV overlap** (2026-11-06). Lightmass warned "Lightmap UV are overlapping by
    N %" for 259 static meshes at 20 % or more in the 45 lighting logs kept from the 2026-11-05
    rebake, while the importer measured 0 %: at 32 px thin parts (planks, rails, sleepers, chair
    legs) are narrower than a texel, no texel centre falls in them, and their lit tops and shaded
    sides baked one light (dark, patchy sleepers on mf-City_Rush's rails). The importer now counts
    a texel shared when two charts touch it, and doubles a prop's lightmap until under 2 %
    ([Pipeline](Pipeline.md#lights)); mf-western: 11 warnings to 4, 5.6 to 7.1 M texels, bake
    8 min as before. `-steps=lightmapcheck` found 55 other maps with a prop at 10 % or more:
    those are rebaked, the rest keep their bake.
16. 🟡 **Polish: Production-quality bake** (Filip, 2026-11-07). Every map was baked at
    `-quality=Preview` (`mflib/ue.py build_lighting`), the base sample counts of
    `BaseLightmass.ini`. Production multiplies them: 32x penumbra rays and 8x shadow rays (our
    UE1-wide soft light radii make penumbras wide, so Preview edges can look grainy), 6x shadow
    edge upsampling, 8x final-gather hemisphere samples, a denser irradiance cache (record radius
    x0.56) and 4-8x photons (less blotchy bounce light in dim rooms). Lightmap resolution, size
    on disk and runtime cost stay the same; only bake time grows (unmeasured, expect 5-10x: most
    maps tens of minutes, mf-nc most of a day). Plan, as the last bake before a release:
    - measure first: bake one indoor map (mf-Hydroworks) at Medium, High and Production, same
      camera shots against Preview, with times; keep Medium where nobody can tell above it;
    - per-map quality (a `quality` key in the map's edits next to `bounces`), so only maps where it
      shows pay for it;
    - bake time: this laptop has 16 threads and Lightmass scales almost linearly with cores. A
      rented many-core Linux VM (64-192 vCPU, spot, a few hours) running the same
      `mfport.py rebake` is the cheapest farm: copy the project and the patched engine, bake,
      copy back `Content` / `Plugins/*/Content` maps. Swarm's distributed farm (SwarmAgent /
      SwarmCoordinator) is Windows-only (.NET, not in the Linux engine: Linux bakes run
      in-process over `-messaging`), so a Swarm farm means Windows PCs with agents
      baking from the Windows checkout. GPU Lightmass (Experimental plugin, needs hardware ray
      tracing) is far faster on an RTX card but uses other settings and looks different
      (and 6 GB VRAM is tight): a separate look test, not a drop-in;
    - while waiting: bakes are already per map and resumable; maps with a tight Lightmass
      importance volume and fewer texels on surfaces nobody sees bake faster at any quality.
    - ⬜ GPU Lightmass test (Filip, 2026-11-08: faster, and maybe better than CPU Medium). Its
      plugin is Win64-only (`GPULightmass.uplugin` PlatformAllowList), so on the Windows side of
      this PC (RTX PRO 500 Blackwell, 6 GB, hardware ray tracing): enable the plugin and
      `r.RayTracing` for the bake only (the game keeps it off), bake mf-OldPrison,
      MF-Test_Facility and mf-ShuttleLaunch, and compare against the CPU Production shots
      already taken (same cameras, `qualitytest.py`): time, look, VRAM (6 GB may not fit the big
      maps), and whether the result differs in kind (GPU Lightmass has its own settings and
      denoiser). If it wins, `light` gets a GPU path for Windows.
    - ✅ measured (2026-11-08) on three small maps, 4 player starts x 2 views (no HUD) per quality,
      grey-level difference to Production (/255; moving bots excluded by taking the median):

      | map | Preview | Medium | High | Production | Preview / Medium / High vs Production (median, 90th pct) |
      |---|---|---|---|---|---|
      | mf-OldPrison | 5.7 min | 6.1 min | 8.9 min | 14.5 min | 1.75, 3.9 / 0.50, 2.0 / 0.25, 1.5 |
      | MF-Test_Facility | 3.7 min | 5.3 min | 8.3 min | 13.5 min | 0.88, 6.4 / 0.62, 2.3 / 0.25, 1.4 |
      | mf-ShuttleLaunch | 2.9 min | 3.7 min | 5.9 min | 10.8 min | 0.75, 2.0 / 0.12, 1.0 / 0.00, 1.0 |

      Medium is within noise of Production and looks the same in the shots; Preview differs
      visibly in places (Test_Facility's 90th percentile). BuiltData size is the same at every
      quality. So `quality` defaults to Medium now (`mapedits.quality`); the three maps are baked
      at Medium; ✅ every map rebaked at Medium on 2026-11-08/09 (86 maps, ~16 h on this laptop).
17. 🟡 **Duplicate maps and lighting variants** (Filip, 2026-11-07). World geometry compared
    across all 88 converted maps (BSP vertices on a 1 m grid, share of the smaller map):

    | maps | shared | difference |
    |---|---|---|
    | DM-Modernity-KF / mf-Modernity | 100 % | same geometry, same 131 lights; only det locks and a domination timer differ. One map baked twice |
    | COOP-GhettoZomburaUpdated / mf-Ghetto | 85 % | same lights and textures; coop adds bots, items and some geometry |
    | MF-BattleBayDarkV1 / mf-battlebaynoonv1 | 82 % | **lighting variant**: night (fog 0,0,47, sky02 dark) vs noon (blue fog, ib_skyblue1); 110 vs 115 lights. Noon is the later, fuller version: 42 vs 28 starts, defence points, Captain zones, minimap |
    | MB-EndagaDam / mf-Hydroworks | 80 % | same author and title ("Hydro"); Hydroworks adds the dam gates and more props |
    | DM-AbandonedMine-KF / mf-AbandonedMine | 77 % | knife-fight cut of the map, 31 of 101 lights |
    | DM-DrainageTrouble-KF / mf-Drainage-Trouble02 | 65 % | knife-fight cut |
    | DM-KnifeWarehouse / mf-Microwarehouse | 62 % | same author, other layout |
    | mf-warehouse / mf-warehouse-merged / mfs-warehouse | 63-66 % | known ([WarehouseMerge](WarehouseMerge.md)) |

    MB-Endaga_Dam is not a dam copy (a Mottyman ball map with dam textures, its title still "Ball
    Park"). Only aligned copies are found: a map moved in the editor would not match.
    - Lighting variants the player picks (BattleBay day / night): UE's **lighting scenarios**
      (a sublevel flagged as lighting scenario holds the lights, sky and fog; the persistent level
      the geometry and gameplay; each scenario is baked into its own BuiltData, chosen at load
      with no runtime cost). Geometry must be identical, so: take noon's geometry and gameplay,
      port the night lights / fog / sky into a second scenario, bake both, offer it as a map
      option (`?Lighting=night`) and in the vote list. The 18 % of geometry noon and night do not
      share must be looked at first (videos), and asked about before dropping the old map.
    - DM-Modernity-KF / mf-Modernity: one level with both actor sets (the KF edition is a
      gameplay subset) halves its bake and download; don't keep the KF name as a map-list alias.
      The same could apply to the other KF cuts if their geometry is a strict subset, otherwise
      they stay separate maps.
    - Done in the converter (2026-11-07, `map_edits.toml`, `mflib/twins.py`), so every conversion
      from a player's own MF gets the same maps. `debug/twin_diff.py A B` lists the world polygons
      one release has that the other lacks (exact coplanar clipping, so BSP splits don't count;
      only faces that see open space), split into pure additions and reworked spots (both
      changed there):

      | pair | B has, A lacks: added / reworked | A has, B lacks: added / reworked | done |
      |---|---|---|---|
      | MF-BattleBayDarkV1 / mf-battlebaynoonv1 | 72 / 124 | 32 / 170 | night = noon's layout, lit at night (`layout_from`) |
      | DM-Modernity-KF / mf-Modernity | 0 / 21 | 0 / 21 | KF not converted (`same_as`) |
      | COOP-GhettoZomburaUpdated / mf-Ghetto | 7 / 45 | 13 / 54 | separate maps (coop) |
      | MB-EndagaDam / mf-Hydroworks | 1149 / 157 | 59 / 160 | separate (a MoBall map) |
      | DM-AbandonedMine-KF / mf-AbandonedMine | 76 / 398 | 1114 / 1805 | separate: the KF one is bigger, not a cut |
      | DM-DrainageTrouble-KF / mf-Drainage-Trouble02 | 30 / 103 | 46 / 313 | separate |

      BattleBay's night release adds no geometry over noon: its extra polygons are the older cut
      of the south shore and the end platforms, which noon reworked (a union stacked night's flat
      shore over noon's ramp), plus sky faces, water sheet sizes and two invisible blockers. So
      both play the noon layout (42 starts, defence points, Captain zones); MF-BattleBayDarkV1
      keeps its own lights, fog, ambient, sky box and music. Same geometry in both: the
      lighting-scenario step above can follow (one map, `?Lighting=night`), halving the download.
18. ✅ **Community game modes** (Filip, 2026-11-07). MF's System folder declares 19 game types
    (`*.int` MetaClass=Engine.GameInfo); scripts without sources exported with UCC to
    `Tools/mfport/work/raw/Classes/<package>`. All are in DF except:
    - ✅ Knife Fight (KnifeFight.KnifeFightGame) and Shotgun Wars (Swars.ShotgunWarsGame):
      `?game=kf` / `?game=sw` ([Gameplay](Gameplay.md#game-modes)).
    - ✅ Mental Race (Racing.MentalRace): `?game=mr` on the 53 maps with two trailer parks and a
      trailer generator ([Gameplay](Gameplay.md#game-modes)). Left: the cars' blue / red bodies
      (Racing.u BugBodBlue01 / red skins, not converted: stock buggies for now). Bots fetch their
      car however far it is (`ADFGameMode::GetBotVehicle`); a 6-bot warehouse test ran laps.
    - trailerGameV2 is in Trailer (`TrailerScatterRadius`).
19. ✅ **Inside-out props, second pass** (Filip's see-through waterfront bench rings, 2026-11-08).
    `StaticMesh17` (the wooden ring round the marble planters) is four overlapping boxes: edges at
    their corners are shared by 3+ triangles, so `t3d.orient_triangles` split it into pieces, and
    two pieces went by vertex normals smoothed round the corners and faced into the wood. Pieces
    whose normals hardly agree now go by ray parity ([Pipeline](Pipeline.md#mfmesh-mflibmfmeshpy)).
    mf-waterfront is converted and rebaked with it. A sweep of the converted meshes: 276 meshes on
    41 maps change; on the 40 most changed, sampled inward faces drop from 4834 to 1656 and none
    gets worse (Express_Delivery's `trailer1` 323 -> 42 of 400, the trains, trash bins, MFS
    warehouse pipes). ⬜ The other 40 maps need `rebake` (convert, import, light) to get it:
    mf-Express_Delivery, mfs-warehouse, mf-Space_station, mf-nc, mf-The_Bank, mf-City_Rush,
    mf-Battlefield, MF-Test_Facility, mf-highwayrun, mf-sawmill, mf-Jungle and 29 with a few
    triangles each (`python3 debug/winding_sweep.py [--check 40]` lists them). ✅ All rebaked
    with it on 2026-11-08/09.
20. 🟡 **Source (VMF) maps** (Filip's mfs-warehouse run, 2026-11-08: open doorways, black sky, missing
    textures, ladders, fences, doors). Done: func_breakable / func_door / func_movelinear /
    func_physbox brushes as closed world geometry; TOOLSINVISIBLELADDER brushes as climbable
    DFLadderVolumes (16), faced by the ladder model in front of them; MFS's own crate model (147);
    unmatched materials take their brush's texture or one for their folder; MF warehouse's sky.
    ⬜ The 312 HL2 models (handrails 100, pallets 59, lights, the ladders' rungs, fences) need MF
    stand-ins (a replacement table: Valve art can't ship); the top of the shelf ladders doesn't
    let go onto the shelf yet; 40 materials are still stand-ins (`unmapped_materials` in its
    level.json).

