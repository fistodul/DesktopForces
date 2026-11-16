# Roadmap

Legend: ✅ done · 🟡 partial · ⬜ todo · ⏸ measured, held back

## Open now (2026-11-15)

What is left, in one place; the sections and numbered threads below hold the history and the
detail. Checked against the code and the commits on 2026-11-15: a mark further down that
disagrees with this list is the stale one.

- **Lighting and bake** (thread 24): GPU Lightmass test on Windows (thread 16); High as the
  default bake quality, yes or no; 4 bounces on the dark maps, yes or no (5 % brighter on
  ZM-SpaceZombura); High against Medium on the Classic and Unverified maps; lighting variants of one map (`?Lighting=night`, thread 17);
  lighting per map against the videos (waterfront reads too bright).
- **Graphics and performance** (thread 24): a weaker phone, the `performance` power profile and
  a fight scene, what the render thread does in a Shipping build; optional Lumen for strong PCs
  (thread 12, nothing measured); grate shimmer (thread 10).
- **Phone** (thread 5): the touch layout editor, tap to fire and look speed under a finger; the
  server browser on the phone.
- **Network and servers**: the Join freeze on Filip's PC (thread 22); a second PC on the LAN
  tab and sorting with several servers, Windows clients (threads 13, 14); played checks of the
  beacon fix, the manifest refusal, five effect sounds and the Predator voice (hand-off
  2026-11-03, thread 23); the console in a packaged Windows Shipping build (thread 4); Windows
  fullscreen and VSync.
- **Gameplay**: bots on ladders and in boats, bot hints (thread 23); the AIRating audit (thread
  3); OmniSound timed playback; shotgun barrel-state animations; ragdolls; new modes; player
  skins; the play-test leftovers (trip laser on vehicles, rotor start, flags on Armadillo, Jeep
  and Heli, the stairs snag, the white text, M1887 alt shot, three sentry guns, HMG and jeep gun
  animations, MAC-10 hand speed, steering).
- **Maps and conversion**: Rage `CollisionMesh` and LodMesh animation; the Source maps' 312 HL2
  model stand-ins (thread 20); replacement art shared between packs; music stand-ins and sounds
  not listened to (thread 9); mod paks in a `Mods/` folder (thread 1).
- **Tooling**: the CI workflow has never run and has no import test; old build folders on the
  download server (thread 7).

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
- ✅ Replacement art for the textures core maps borrow: generated stand-ins for ~850 (`Tools/artgen/lookalike.py --auto`), the see-through ones (lattices, trees, palms, splats) too since thread 9: every community map runs without the classic pack
- ✅ Sky: each map's SkyZone captured into a cubemap for the sky material
- ✅ Minimap textures (MiniMapInfo): MF tiles them (NumTexturesWide × High, 255 px each, 28 maps), stitched into one by `mflib/minimap.py` (only the first tile used to be stretched over the map); maps without a MiniMapInfo (56, VMF ones too) get one drawn from their floors (lighter higher up, lines at steps)
- ✅ Textures straight from the packages: S3TC copies (MF's D3D look), palette alpha for HUD art
- ✅ Mounted gun aim (2026-11-14): the gun aims at what the middle of the gunner's view is on (a trace along the view, 5 m at least) instead of parallel to it, so its crosshair sits on the view's once the turret has caught up; seen on the Armed APC at three ranges
- ✅ Phone frame rates (2026-11-08, Pixel 9a): the list is the rates that divide one of the panel's refresh rates (60 / 120 Hz: 120, 60, 40, 30, 24), and the cap goes to the frame pacer too (`FPlatformRHIFramePacer::SetFramePace`: Swappy picks the panel mode; `t.MaxFPS` alone left the phone at 60). The cap steps down when the frame rate stays under 90 % of it for 30 s (logged, this session only). Measured 59.9 FPS at 60 in Mental Race with 4 bots. That 9a has Smooth Display off (`peak_refresh_rate` 60), so Android holds 60 Hz whatever the game asks: a 120 cap stepped down to 60 after 30 s
- ✅ Kill credit left over from the code review: seeker mines, trip lasers and the Predator read their owner's controller when they hit (none once he was dead); they keep it from the placing now, like projectiles, trip bombs, molotovs and the sentry gun (`DF::PlacerCredit`). A gunner's or driver's shots need a living gunner anyway
- ⬜ Hand-off 2026-11-03, open after the last feedback round:
  - Not tried in game yet: trip laser on vehicles, exiting a rolled vehicle (view level), rocket APC crosshair, rotor start, flags on Armadillo, Jeep and Heli
  - ✅ (2026-11-13) Review of `DFMenu`, `DFHUD`, `DFCheatManager` and the download code. Fixed: `RunAs` is for admins only (on a `?Cheats=1` server anybody could run `Kick` through a logged-in admin: the command ran with the other player's rights); a pack of which only some paks mounted read Installed and Retry did nothing (what mounted is let go, the pack reads Failed); `LocalManifest.txt` is written to a temporary file and renamed; Esc on the Connecting page cancels the connection instead of hiding it; a rename in game turns spaces into `_` as joining does (admin commands take a name as one word). ✅ (2026-11-14) the rest: every command that names someone goes through one lookup (`FindTarget`: a player, else a pawn's name, else the nearest of a class), `SetToSelf` / `...Looking` act on the actor itself and a player's name may hold spaces (`SetTo red baron MaxHealth 250`); `Kick` takes players only. The HUD asks the content for each picture once, found or not (weapon icons, power bar, muzzle flare, minimap), and looks for the map's info again while there is none. A download manifest whose file names aren't plain file names is refused (the server chooses them, and they become paths that are written, mounted and deleted; not tried against a server)
  - ✅ (2026-11-13) Beacon kept announcing when a host joined another server directly: it stops itself once the world is a client's (`UDFServerBeacon::Tick`). Read, not played: needs a listen host joining a second server
  - ✅ (2026-11-13) `AdminLogin` no longer sends the password: the server sends a one-time challenge, the client answers HMAC-SHA1 of it keyed with the password; three wrong answers lock an address out for a minute, doubling up to an hour. Checked with a dedicated server and a client (wrong refused and counted, right accepted). A recorded exchange still allows guessing a weak password offline, and nothing after the login is encrypted (UE's packet encryption needs a key exchange). `SwitchMap` is gone: `Admin servertravel map?game=mode` is the engine's own command
  - `mf-warehouse` explosive table: rocket 202 at the centre (135 x 1.5 hardcore), 100 at about 4.5 m, 8 at the 9 m edge (mutPack's x2 for players' rockets on soldiers isn't ported: ImprovedRL buffs rockets)
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
- 🟡 Vehicles: Buggy, Humvee, Truck, Armadillo APC as physics boxes with raycast wheels tuned from `handling.hnd2` / `.COL` (springs scaled to MF gravity), MF meshes, generators, enter/exit (E), run-over, explosives-only damage. ✅ passenger seats (shooting from them), Armed APC turret, wheel blow-outs, petrol caps, bots driving (MF road graph) / riding, MoBall bot drivers (thread 23); ⬜ bots in boats (`bBotsDrive = false`, helis and pipers too)
- ✅ Ladders: grab when close / falling in, climb down from the top, floor walks again; ✅ swimming / drowning, water / pain zones
- ✅ Map actors: teleporters, placed items (respawning), periodic sounds, movers (scenery, spinners, bump doors), invisible walls, spawn room doors, coop maps' placed RageBots, particle effects (ParticleEffect / FireyExample: fires, smoke, sparks, rain); ✅ trigger / special event chains, bells, LaserBeam (thread 23, `World/DFEvents.cpp`; Treasure_Finders' lighthouse only switches lights and is left as it is: Filip, 2026-11-14)
- ✅ MF-mods, part of the game (`Game/`, `Weapons/`, `Vehicles/` subfolders, [Modding.md](Modding.md)): Molotov, KOH, Totem, TripLasers, SeekerMines, SentryGun, TugGame, CoopMode, ZombieGame, deagle, grenLaunch, PredatorMissile, ArmedAPC, arenaMode, MoBall, battleRoyale, TurboCars, boat, jeep, mutPack changeDamage
- ⬜ New modes to consider (reminders, nothing designed yet):
  - Bomb defuse: attackers carry one bomb to a site and plant it, defenders defuse it, in rounds without respawning. Mostly built from what we have: sites = the maps' `AHoldoutPoint`s (KOH / Holdout / Arena placement; maps with fewer than two get spots added like other mode spots), round flow and "stand on it for N s" = Arena (`Game/Arena`), the bomb = Detonation's carried key (`AFlag`, drop on death, return when untouched) and its countdown / blast; new: plant and defuse as interrupted timed actions, which side attacks swapping each half
  - Sky surfing: MFS `cfg/gamemode/surf.cfg` (`sv_airaccelerate 200`): Source-style air strafing on slanted surfaces; needs air control in `UDFCharacterMovement` and surf maps
  - Injustice (MFS `mp_injustice`, `cfg/gamemode/injustice.cfg`): meant as blue players helping the map's NPCs fight a few red ones (one red per nine blue in `hl2mp_gamerules.cpp`; blue support kit: medkit, physcannon, bugbait; red attack kit, 1 hp/s regeneration, own `info_player_god` spawns; NPC sides in `InitInjusticeAIRelationships`). DF has no NPCs, so a simpler take: the red are Zombie mode's NEMESIS in all but name (lone side scaled by the ratio, regeneration, harder hits), so build it as a ZombieGame preset rather than a new mode: `?Infect=0`, red nemesis-boosted from the start at a set ratio (MFS 1 : 9) instead of only when alone against 5+, red keep ranged weapons (`?ZombieWeapons`), human bodies, blue topped up with bots in place of the NPCs. Unlike a nemesis round (dead humans wait for the next round, the nemesis respawns) blue respawn, so the round needs a cost: lives on both sides (blue a shared pool, about 2 x their count, or waves every 15 s; red about 3 each; out of lives loses, at the clock blue win or the larger share of lives left), or an objective (blue hold a Holdout point red must break). Balance to be tried in play
- ⬜ Mods for the packaged game: a `Mods/` folder of paks scanned at start, mod maps in the menu (one item with thread 1's open part)
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
- ✅ First-person weapons: MF's arms models (PlayerViewMesh) with their select / fire / reload / zoom animations, MF PlayerViewScale / PlayerViewOffset; ✅ grenade pin pull wind-up, knife catch, HMG deploy, view bob; ⬜ shotgun barrel-state animations (only the `*Full` ones play), a bob of the weapon itself
- ✅ Third-person soldiers: MF helmet / balaclava models, team and captain skins, MF's animation choice (Pawn.uc Get*Anim: weapon sets, zoomed poses, strafes, crouch, swim, jump / land, throws, deaths), carry meshes in hand, drivers and seated passengers; ✅ saved player model and preferred team choice; ⬜ ragdolls
- ✅ Effects (MF RageSprite particles → Cascade, `mfport.py effects`): explosions (grenades, rockets, trip bombs, mods), vehicle explosions with burning debris and ground fires, tyre smoke, damage smoke, rocket/Predator fire trails, wall hits, blood, map emitters; ✅ tracers, shell cases, bullet holes and scorch marks (decals), smoke grenades, muzzle flashes, bubbles, water impact rings (the rings' look not compared with MF's)
- ✅ Menus: title screen over the MFS pictures with the start-up theme, play / join / options (render scale, brightness, mouse, FOV …) / credits / quit, Esc menu in game; ✅ server browser (333networks master server protocol and LAN: thread 13)
- 🟡 Touch controls (mobile): built (thread 5: drawn signs, layout editor, look speed, tap to fire); ⬜ dragging, tap to fire and look speed under a finger on the phone

## Maps
- ✅ mf-warehouse converts and builds
- ✅ More maps convert and bake: Carpark, Toreford, MF-Filip, mf-Microwarehouse, Mf-MicroBunker, mf-MicroTower. ✅ the rest, all converted and lit since 2026-11-05 (airport, dockyard, ghetto, hydroworks, polar, prehistoric, rail quarry, sawmill, space station, tropics, waterfront, western …)
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
   - ✅ Android: one APK, packs downloaded in game (thread 7; the OBB of 2026-10-29 is gone).
2. ✅ **First-person rig accuracy** (2026-10-28): RotOrigin applied as UE1 does after the .psk Y
   flip (the pitch cants the gun about its barrel instead of raising it), MF's weapon roll ×
   Hand (`ArmsRoll`), and CalcDrawOffset's 0.9 / FOV offset scale.
3. ⬜ **Audit ported defaults for inherited values**: `mfport.py defaults <Class> [Prop...]` prints the
   effective values down the parent chain (2026-10-28). Weapon audit: view offset, scale and roll
   all match. AIRating differs (ours / MF): knife 0.5 / 0.1, Deagle 0.5 / 0.45, grenade launcher
   0.55 / 0.45, Molotov, seeker mines and trip lasers 0.5 / 0.4. Check bot weapon choice before
   adopting MF's values; other properties are unaudited.
4. 🟡 **Developer console in Shipping**: the option is built and compiles in Shipping. Opening it
   in a packaged Windows build is still unchecked.
5. 🟡 **Android: first runs on a Pixel 9a** (2026-10-28, wireless adb).
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
   - 🟡 (2026-11-13) Button signs instead of words (drawn lines, no textures), a layout editor (Esc menu >
     Options > Controls > Touch buttons, or `DFTouchEdit 1`: drag, Reset, Done saves the offsets), stick look
     speed (Options > Controls) and "touch the right side to fire" as an option. The sticks' look was a turn
     per frame, twice as fast at 60 FPS as at 30: it is a rate now (150 degrees a second at full tilt, the
     speed it had at 60). Seen on the desktop only (signs, the editor's bar and a moved button in a
     screenshot with `bAlwaysShowTouchInterface`): dragging with a finger, tap to fire and the look speed
     need the phone.
   - ✅ Options by touch (2026-11-08): Video page, Advanced Graphics, the FPS combo and Apply all work by tapping; the game type list shows the new modes.
   - (Superseded by thread 7: one APK and in-game pack downloads.) From 2026-10-29 the data was in an OBB: a 103 MB APK and a 1.39 GB OBB for two maps
     (classic 1.22 GB of it). It runs from the OBB on the 9a. Install with the generated
     `Install_DesktopForces-arm64.sh` ([Building](Building.md#android)).
6. 🟡 **Bots and vehicles** (2026-10-29):
   - Fixed: covering bots no longer pick their own vehicles, drop a vehicle they were walking to
     when given an order, and drive to the leader if already driving (1cbb8ac).
   - Untested: a full ride-along. On mf-Tropics the bots headed for the player's Humvee, but
     navmesh gaps and the fighting got in the way.
   - Test recipe: `-LogCmds="LogDF Verbose"`; TeleportTo Buggy 150 (DFEnterSeat needs to be
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
9. 🟡 **Replacement art for borrowed classic assets** (user idea, 2026-10-29): start with the
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

11. ✅ **Systems performance follow-up** (2026-10-30): StateTree dependencies removed after
    an asset-reference audit; editor build and six gameplay suites pass. Bot decisions remain
    at 5 Hz, with staggered initial timers reducing measured 48-bot timing spikes. Animation
    budgeting was tried and removed (no frame-time gain). MF_TestRange was regenerated and
    rebaked after its old resave lost the gameplay actors. A regression test now verifies four
    neutral starts, two per team, team selection and navigation coverage. VehicleProject builds
    and runs at 30/60/120 FPS; DF recovery/turbo checks pass at all three caps. No matched test
    establishes a reason to migrate vehicle physics. Comparison and replacement-art results are in
    [Performance](Performance.md#systems-follow-up-2026-10-29). ✅ Spatial prop instancing is in:
    measured, and on by default since 2026-11-04 (`instance_props`, 8 placements a cell).
12. ⬜ **Optional dynamic lighting for strong PCs: Lumen / "Lumen Lite"** (Filip, 2026-10-29). Today
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
13. 🟡 **Server list: LAN and internet through the 333networks master** (Filip, 2026-10-30). Filip
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
      list in `DefaultGame.ini`, an `-nomaster` switch for private servers;
    - client: TCP list request (`\gamename\..\location\0\validate\..\final\` then
      `\list\\gamename\desktopforces\final\`), then query each server itself over UDP for name,
      map, mode, players and ping (as UE1 did); a LAN tab broadcasts the same `\status\` query to
      the query ports on the local subnet, so one protocol covers both;
    - ✅ menu (2026-11-01; sorting, filter and LAN 2026-11-13): Join has an Internet and a LAN list (LAN: one `\status\`
      broadcast to query ports 7778-7797, `DFServersLan`), columns that sort when pressed, a filter box (words in
      the name, map or game type), a mark on servers whose map isn't installed; the first press picks a server, a
      second one joins. Checked: a dedicated server on this PC found by the broadcast, the page in a screenshot
      (`DFMenuPage 21`). Not tried: a second PC on the network, sorting with several servers, the phone;
    - hosts forward UDP 7777 and 7778 (no NAT traversal in this protocol); IPv4 only; test with a
      local master first (333networks MasterServer runs anywhere), then `master.nofisto.com`.
14. ✅ **Dedicated server builds** (2026-11-06): `python3 Tools/package.py Linux --server` builds
    and cooks `DesktopForcesServer` inside Epic's engine image (the Launcher engine has no server
    targets) from a copy of the project, and packs a tarball that runs on any x86-64 Linux with
    glibc 2.28+, no Docker or engine there ([Hosting](Hosting.md#ways-to-run-one)). Tested: unpacked
    in a fresh folder, run natively, an editor client of the same version joined it. 2026-11-08:
    the full-map build after the rebake (`20261108`, every mode incl. Knife Fight / Mental Race),
    the Pixel 9a joined it (Knife Fight, bots) and an editor client (Mental Race). ✅ The VPS run is thread 22
    (`mf.nofisto.com:7787`). ⬜ Windows clients against it.
15. ✅ **Prop lightmap UV overlap** (2026-11-06). Lightmass warned "Lightmap UV are overlapping by
    N %" for 259 static meshes at 20 % or more in the 45 lighting logs kept from the 2026-11-05
    rebake, while the importer measured 0 %: at 32 px thin parts (planks, rails, sleepers, chair
    legs) are narrower than a texel, no texel centre falls in them, and their lit tops and shaded
    sides baked one light (dark, patchy sleepers on mf-City_Rush's rails). The importer now counts
    a texel shared when two charts touch it, and doubles a prop's lightmap until under 2 %
    ([Pipeline](Pipeline.md#lights)); mf-western: 11 warnings to 4, 5.6 to 7.1 M texels, bake
    8 min as before. `-steps=lightmapcheck` found 55 other maps with a prop at 10 % or more:
    those are rebaked, the rest keep their bake.
16. ✅ **Polish: Production-quality bake** (Filip, 2026-11-07; decided: Medium by default, High
    per map where it shows, thread 24. The GPU Lightmass test below is the one part left). Every map was baked at
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
      already taken (same method as `Tools/mfport/debug/bakequality.py`): time, look, VRAM (6 GB may not fit the big
      maps), and whether the result differs in kind (GPU Lightmass has its own settings and
      denoiser). If it wins, `light` gets a GPU path for Windows.
    - ✅ measured (2026-11-08) on three small maps, 4 player starts x 2 views (no HUD) per quality,
      grey-level difference to Production (/255; moving bots excluded by taking the median):

      | map | Preview | Medium | High | Production | Preview / Medium / High vs Production (median, 90th pct) |
      |---|---|---|---|---|---|
      | mf-OldPrison | 5.7 min | 6.1 min | 8.9 min | 14.5 min | 1.75, 3.9 / 0.50, 2.0 / 0.25, 1.5 |
      | MF-Test_Facility | 3.7 min | 5.3 min | 8.3 min | 13.5 min | 0.88, 6.4 / 0.62, 2.3 / 0.25, 1.4 |
      | mf-ShuttleLaunch | 2.9 min | 3.7 min | 5.9 min | 10.8 min | 0.75, 2.0 / 0.12, 1.0 / 0.00, 1.0 |

      Medium is within noise of Production and looks the same in the shots of these three (thread
      24 later found maps where High shows over Medium: dark rooms, snow); Preview differs
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
    warehouse pipes). ✅ (done, below) The other 40 maps need `rebake` (convert, import, light) to get it:
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

21. ✅ **Vehicle bugs** (Filip, 2026-11-09). The expiry flash now covers every mesh the vehicle
    owns (axles, suspension, turret, rotors too), not just the body and wheels; the blast is
    MF's `HurtRadius(300, 400 uu, 200000 momentum)` again (it was 150 and pushed nothing), so a
    buggy beside an APC takes up to 72 of its 100, enough to finish a damaged one as in MF.
    `DesktopForces.Gameplay.VehicleBlast` checks both.
22. 🟡 **Online server testing** (Filip, 2026-11-09). `mf.nofisto.com:7787` (query 7788; MF's own
    server keeps 7777/7778/7780) runs mf-Sardine_City with 1 bot under systemd and is listed on
    master.nofisto.com. Editor runs carry `ProjectVersion=20261108` (`DefaultGame.ini`) to join it
    while the replicated code matches that release. Done: the master list is read on threads of its
    own, replies polled every frame (ping no longer up to 50 ms high). Before, a `ParallelFor` in a
    background task did the master queries: a game thread waiting on the task graph can run such
    work itself, sleeping in `ReadUntilFinal` for up to 16 s - Filip's freeze and lag on pressing
    Join Game (Linux; lag only while the list loaded). Not reproduced on a 16-thread PC either way;
    recheck on Filip's. Server CPU ~8 %, ping 26-43 ms.
23. 🟡 **Play-test on the server, mf-Sardine_City** (Filip and testers, 2026-11-10).
    - ✅ Vehicles in the ground: clients only. A client simulated the replicated body (gravity,
      no suspension) and the engine's physics replication froze it where it lay once the
      server's slept: 3 of 9 cars in view sat 55-90 cm low. Clients keep the body kinematic and
      follow the server now ([Architecture](Architecture.md#networking)); checked with a dedicated
      server and a client (parked heights, driving).
    - ✅ MoBall: the ball stands in a soldier's way and only creeps when leaned on, as MF's
      (`Physical.Bump`); vehicles and shots move it as before ([Gameplay](Gameplay.md)).
    - ✅ CTF: a capture left the flag with the carrier (a late overlap event after the flag had
      gone home), so every step into the base scored; flags only count touches from within
      reach now, and only on foot (MF). The flag has a model: staff and team emblem.
      Detonation's key checked the same way (walk-in insert, countdown, restart).
    - ✅ Replacement art: sign sheets keep their layout and get whole words (the shop front at
      Hake st. 29 reads PIZZA / MARKET / BOOKS / GROCER / OPEN), snow is snow again, the traffic
      cones are solid, doors, vending machines, barrel ends and windows in walls are drawn as
      such; 203 automatic entries changed ([Pipeline](Pipeline.md), `lookalike.py --auto
      --reclassify`). Looked at in game on mf-Sardine_City only.
    - ✅ Bots on mf-Sardine_City never left their bases, on any mode: the navmesh shut the
      125 cm spawn room doors (agent radius 38.25 over 19 cm cells erodes three cells; 38 is
      two), and a walk broken off by a fight counted as "goal out of reach" for 30 s. Radius 38,
      every navmesh built again, bots ask again after a broken-off walk
      ([Gameplay](Gameplay.md#loadout)). Found by 4 minute bot matches of every mode with
      `-LogCmds="LogDF Verbose"`: grep `no full path`, `out of reach`, and the scores.
    - ✅ Bots and vehicles: a bot that chose a car kept walking to its goal instead (51 of 60
      tries given up), and took cars for trips of a few metres; fixed, and on mf-Sardine_City
      bots now drive across, take, drop and return both flags.
    - ✅ Safecracker: explosives never hurt the safe (world-static collision; radial damage
      only reaches dynamic objects). Crack, steal and deliver walked through after the fix.
    - Walked through with `DFWalk` (not teleports) after the fixes: CTF (also on a dedicated
      server), Detonation, Safecracker, KOH, Trailer, Captains. Bot matches of all 18 modes
      ran without warnings; native suites 12 / 12.
    - ✅ COOP-CoopOneUpdate's 12 starts cut off from the rest: the map's own design. Humans play
      red (CoopMode.ChangeTeam 1) from the starts at the bottom; the twelve blue starts are a
      sealed room 70 m up, a pen for the mode's own bot team, and both flags stand behind a
      red-only door at the far end (the finish). Its bots log `no full path` for that reason.
    - ✅ MoBall's own maps had no goals and dropped the ball into the void at the map's origin
      (MB-Ballpark, MB-DesertArena, MB-Endaga_Dam, MB-ShippingArena: `PropGenerator`,
      `MoBallGoalZone`, `MoBallDestructZone` were not supported, only MF maps' trailer parks).
      The converter writes the zones as boxes, the mode reads them and the generator
      ([Gameplay](Gameplay.md)); the four maps were patched without a rebake. Checked on
      MB-Ballpark: a ball brought into each goal scores for the other team, one in the pit
      comes back without a score (`DFBring MoBall`). ✅ (2026-11-14) Bots on foot went for the
      ball, which often lies where no path leads (that map's middle mound): they fetch the
      nearest free car now and walk at the ball only from close by, as MF's did. 7 minutes with
      8 bots on MB-Ballpark: 188 boardings, 78 hits on the ball, a goal, 1 `no full path`
      (63 before in 4 minutes).
    - ✅ `TeleportTo Name [Dist]` takes over `DFTeleportNear`; `DFWhere` reports a spot
      ([Commands](Commands.md)).
    - ✅ White paving for the town's level ground (Filip liked the white bricks the snow
      texture had become): `retexture` in `map_edits.toml` gives the flat world faces at ground
      level a texture of ours (`DesktopForces.Paving.snow_bricks`), the snow heaps, roofs and
      everything sloped keep the snow ([Pipeline](Pipeline.md#map-edits-toolsmfportmap_editstoml));
      the map was converted, imported and baked again (44 min), so a reimport gives the same.
    - ✅ MF's waving flags: `CTFFlagMesh` (CTF) and `VehicleFlagMesh` (the pennant of a driven
      vehicle) come through the skeletal step with their four sequences; without the classic
      pack the flat stand-ins stay.
    - ✅ Kill credit: seeker mines, trip lasers and the Predator keep who placed or fired them
      from that moment (`DF::PlacerCredit`), as the sentry gun did; a dead player's devices
      still score for him while he stays on the team.
    - ✅ Sounds only the server played: `DF::PlaySoundAtLocation` plays on the machine that
      calls it, and seven calls sat in server-only code, so nobody on a dedicated server heard
      them: the flag taken, the trailer locking down, a turret reloading, the seeker mine's
      beeps, the Totem and the Predator's "Nuke the whole area". They go out through the
      replicated effect actor now (`ADFEffectActor::SpawnSound`) or the soldier's voice
      multicast. Found by reading the Totem and Predator code, which had not been reviewed.
      2026-11-13: a client of a dedicated server gets the seeker mine's beep (`Effect sound ...
      (client)` with `-LogCmds="LogDF Verbose"`); the other five on that route and the Predator
      voice were not triggered. Same day on this round's code: native suites 12 / 12, bot matches
      of all 18 modes without errors (no flag was taken in the 4 minute CTF one).
    - The other modes' objects (safes, locks, flag bases) are plain shapes without the classic pack.
    - ✅ (2026-11-14) UE1's events, found by listing the map actors no code reads: touch and
      shot `Trigger`s, `SpecialEvent` (message, damage, death, healing, a sound there or for
      everyone), `Dispatcher` and `Counter` are converted (`World/DFEvents`,
      [Gameplay](Gameplay.md)); only chains that end in something are kept, which leaves 7
      maps: kill planes (DM-Warship's three at 50 each, MF-BattleBay's 153 triggers in both
      its versions, MF-BangBash, mf-Modernity), mf-City_Rush's alarms and
      mf-Treasure_Finders' school bell (its dispatchers only switch lights). Checked in game on DM-Warship (1000 health: 950, 900, 846
      falling through the planes), mf-City_Rush (the alarm, again every 2 s while standing in
      it) and mf-Treasure_Finders (the bell); the seven maps were patched without a rebake.
      mf-City_Rush's alarm sound is in the `cityrush` package, which is not listed under
      `[community.packages]` and so sits in the unverified pack: silent without it.
    - ✅ (2026-11-14) The rest of the events: movers answer events (`TriggerOpenTimed`,
      `TriggerControl`, `TriggerToggle`), shots (`bDamageTriggered`) and fire their own Event
      once open; `ExplodingWall` / `BreakingGlass` break with their sound; any actor's
      `AmbientSound` plays (OmniSounds, fires, MusicEvents as speakers: 72 sounds on 13
      community and unverified maps, mf-highwayrun 30 of them). Checked in game: DM-Asylum's
      two glass panes break when shot (GlassSmash), mf-Battlefield's bunker button opens its
      door, MF-CrazyHouseV2's shot trigger opens its door, mf-highwayrun has its 30 sounds.
      17 maps reconverted and patched without a rebake (`--steps patch` places ambient sounds
      now too).
    - Left as it is (Filip, 2026-11-14): `TriggerLight`. 77 on 5 maps (mf-Treasure_Finders 39,
      community) are switched by events; 68 of them start off in UE1 (`bInitiallyOn` false) but
      are baked on here, so those rooms are brighter at the start than in MF and never change.
      Baking them off would match MF's start but lose the light for good; switchable means
      dynamic lights (cost, and none on phones).
    - ✅ (2026-11-14) Sprites: visible `DT_Sprite` actors (ScaledSprite decorations,
      mf-BraveDwarfes' 198 `LaserBeam` bubbles) as one camera-facing mesh per map, a draw per
      texture: 1079 on 5 maps (mf-Jungle 838, mf-BraveDwarfes 198, MF-BloodWarzV1 22, mf-nc 14,
      MB-EndagaDam 7; the other ScaledSprites are hidden or editor icons). Checked in game:
      mf-Jungle's grass faces the camera from the front and side.
    - ⬜ **OmniSound timed playback**: port `SoundToPlay`, `ConstantTimeComponent` and
      `RandomTimeComponent` from the original class: each timer calls `ClientPlaySound` for
      every player (level-wide). Only retail mf-airport sets it in the converted maps: four flybys,
      with constant times of 58, 82, 134 and 202 seconds. Keep these one-shots separate from
      `AmbientSound` loops; reconvert/patch mf-airport and check timing and audibility in
      game, including a connected client.
      Bot hints (`RageDefensePoint` 706, `Ambushpoint` 365) are unused too.

24. 🟡 **Graphics options and bake quality: measure, then set the defaults** (Filip,
    2026-11-12).
    - ✅ Options measured (2026-11-14): `gfxbench.py` three times at 1080p and twice at 2160p,
      one of each in reverse order, three fixed cameras on community maps, each scene started
      cooled (GPU 58 C / CPU 65 C) with temperatures, clocks, power and the driver's slowdown
      flags logged (the method of the UE5 / O3DE comparison). Table, conditions, findings and
      proposed defaults in [Performance](Performance.md#graphics-options-measured-2026-11-14):
      the frame is the render thread's on this laptop (4.3 - 8.5 ms whatever the setting, and
      the CPU package at its 105 C limit when uncapped), MSAA is the one costly option (none to
      2x: +22 % GPU time at 1080p, +58 % at 2160p), anisotropy is free, Low's mip bias costs
      the most picture for no time, and view distance, GI, reflections, shading and
      post-processing change nothing (baked lighting, forward shading). ✅ The same on the
      laptop's integrated Intel GPU (`--gpu=intel`, three times slower): MSAA none to 2x +18 %,
      textures 0 to 3 within 2 %; Low's mip bias saves neither GPU time nor memory there either
      (128.5 MB of textures at every level). ✅ The menu lets a player set anti-aliasing,
      textures and effects apart from the preset ("Medium*"). ✅ Textures one level up in every
      preset, as Filip leaned (2026-11-14): full size and 2x filtering on Low, 4x Medium, 8x High
      and Epic, 16x Cinematic (`UDFUserSettings::PresetQuality`); Advanced Graphics' "Texture
      filtering" keeps Blurry (half size, its own setting `bHalfSizeTextures`) and Off below 2x;
      Controls shows keyboard and touch in sections, the one the device doesn't use closed.
      ✅ The phone (Pixel 9a, 2026-11-14,
      [Performance](Performance.md#on-a-phone-pixel-9a-2026-11-14)): 120 FPS at every setting,
      GPU about 4.4 ms, MSAA the only cost, the texture defaults hold; Render scale did nothing
      there (no upscale pass without mobile HDR) and now sets the content scale. ✅ Phone as a
      client of a PC dedicated server over the LAN: joins, admin login, suicide counted, chat.
      ⬜ A weaker phone before raising the phone preset; ✅ the phone's synchronous loads measured:
      149 at spawn, about 20 first-use ones in play costing a frame or two each
      ([Performance](Performance.md#on-a-phone-pixel-9a-2026-11-14)); ⬜ the `performance` power profile, and a fight
      scene for effects quality; ⬜ what the render thread spends its 5 ms on in a Shipping build.
    - First numbers (2026-11-13, mains power, 12 bots, CTF on mf-Sardine_City, `stat dumpframe`, 8
      frames each): the HUD took 0.59 ms of a frame's game thread, 0.42 ms after its "loadout
      available here" sign stopped searching the map's loadout sources every frame
      (`DrawHealthMF` 0.14 -> 0.03 ms). What is left: minimap 0.16 ms, captain markers 0.04 ms (drawn
      in every mode), inventory, identify and game info about 0.05 ms each. Every part has a named
      counter now (`STAT_DFHUD_*` in `stat dumpframe -ms=0.002`). A phone's CPU is several times
      slower: measure there before more caching.
    - Bake quality, with it. ✅ The per-map `quality` choice of #16 (below). ⬜ The GPU Lightmass
      test on the Windows side (#16). 4 bounces: measured on 2026-11-01 on mf-waterfront and
      mf-warehouse ([Performance](Performance.md#dark-surfaces-what-worked-2026-11-01): under
      1.2 % brighter, 15 % more bake time, not used). On ZM-SpaceZombura, a dark map with zone
      ambient 0 where bounced light is all there is, it does show (2026-11-16, High, 20 cameras
      against the 2-bounce bake): the views are 5.4 % brighter (2 to 8 grey levels of 255 a
      view), the same look otherwise, for the same bake time (2.8 min). Not kept: a brighter map
      is a look to decide, not a quality (`bounces = 4` in its `map_edits.toml` section, then
      `map`, `import --steps maps`, `light`). ⬜ Filip: 4 bounces on the dark maps
      (ZM-SpaceZombura, MF-TurpowiaV1), yes or no. ⬜ The meshes Lightmass still reports with
      overlapping lightmap charts (what #15 left: charts smaller than a texel).
    - ✅ High against Medium on all 22 maps of `Content/Maps`, measured (2026-11-15,
      `Tools/mfport/debug/bakequality.py`: up to 10 player starts x 2 views per map, no HUD, no
      bots). Change: the same camera with the Medium bake and with the High bake, absolute
      grey-level difference per pixel (/255) averaged over the picture, median camera. It says how
      much the picture changed, not whether it got better: that is the last column, by eye on the
      most-changed views side by side. Two screenshot runs of one bake differ by 0.24-0.28, and
      so does a second Medium bake of the same map (MF-Aquarius, MF-Stuck: bakes repeat), so
      anything above about 0.3 is the quality setting. Medium minutes are from the 2026-11-08/09
      rebake logs (power state then not recorded), High on mains:

      | map | Medium | High | change | seen in the shots |
      |---|---|---|---|---|
      | ZM-SpaceZombura | 1.4 min | 2.7 min | 3.70 | Medium's dark blotches on floors and walls are gone: High |
      | mf-Sardine_City | 37.4 min | 38.7 min | 1.10 | grey blotches on the snow heaps gone: High |
      | MF-TurpowiaV1 | 2.5 min | 4.2 min | 0.44 | grey smudges on the white walls gone in some rooms: High |
      | mf-Tropics | 4.7 min | 7.2 min | 1.06 | leaves and cans lit a little more evenly: High (Filip: slight is what these changes are, and it bakes in under ten minutes) |
      | MF-Aquarius | 1.5 min | 2.2 min | 0.83 | nothing to point at; a step on its columns (below, fixed): High (Filip: the more exact light) |
      | mf-Space_station | 11.4 min | 17.7 min | 0.81 | nothing to point at: High (0.7 and over) |
      | MF-Test_Facility | 5.2 min | 8.6 min | 0.77 | nothing to point at: High (0.7 and over) |
      | MF-RandomLand | 0.5 min | 0.7 min | 0.71 | nothing to point at: High (0.7 and over) |
      | mf-Toreford | 39.5 min | 37.4 min | 0.70 | nothing to point at: High (0.7 and over) |
      | mf-Pobojowisko_VerII | 11.8 min | 13.7 min | 0.60 | no gain |
      | mf-BraveDwarfes | 5.7 min | 7.1 min | 0.59 | no gain |
      | mf-Tajga | 19.5 min | 25.5 min | 0.59 | no gain |
      | mf-City_Rush | 54.8 min | 56.5 min | 0.59 | no gain |
      | MF-Stuck | 0.4 min | 0.5 min | 0.56 | no gain |
      | mf-Worlds_Peak | 1.6 min | 3.0 min | 0.56 | no gain |
      | mf-Treasure_Finders | 21.7 min | 25.4 min | 0.53 | no gain |
      | mf-Prehistoric | 8.3 min | 9.9 min | 0.53 | no gain |
      | DM-Warship | 6.5 min | 7.0 min | 0.50 | no gain |
      | BR-thebigisland | 44.7 min | 60.9 min | 0.48 | no gain |
      | COOP-CoopOneUpdate | 4.3 min | 7.1 min | 0.47 | no gain |
      | dm-HolidayTown | 2.9 min | 4.2 min | 0.36 | no gain |
      | MF-Filip | 0.4 min | 0.6 min | 0.34 | no gain |

      All 22: 287 min at Medium, 341 min at High (1.2x; the small maps up to 1.9x, the big ones
      hardly slower). Three gained plainly: bounced light on plain pale or dark surfaces (the two
      dark maps with zone ambient 0, and Sardine City's snow). The change figure does not rank
      the gain (MF-TurpowiaV1 was kept at 0.44 while maps at 0.7-0.8 were not): the verdicts are
      an eye on three most-changed views a map, so a map above about 0.5 probably has places
      that are better at High which nobody looked at. Filip, 2026-11-15: the gains are slight by
      nature, slight counts. Nine maps carry `quality = "High"` in `map_edits.toml` and are baked
      so: the three, mf-Tropics, MF-Aquarius, and the four at 0.7 and over (mf-Space_station,
      MF-Test_Facility, MF-RandomLand, mf-Toreford). The other 13 are at their Medium bake, and
      Medium is still the default. ⬜ Decide whether High becomes the default for every map
      (54 min more for these 22; nothing got worse at High that was looked at).
    - ✅ Lightmap charts of round props (2026-11-15/16). MF-Aquarius's ten-sided column showed a
      step down its shaft in the High bake, mf-Sardine_City's snow heaps hard-edged patches at
      any quality: the importer's box projection cuts a smooth round prop into side charts, and
      the light is baked chart by chart. What was tried on the column, all at High: box at 64 px
      a step, at 128 px bright blotches, at 256 px clean; its texture unwrap at 64 px clean
      (at 512 px, what it needs to share under 2 %, 14 MB more built data).
      - The column takes its unwrap by a map edit, `lightmap_uvs = { StaticMesh97 = "texture" }`
        (its many rings measure worse than the box, so no rule picks it).
      - The importer now takes the texture unwrap of a smooth round prop by itself, under
        limits ([Pipeline.md](Pipeline.md#lights), "Smooth round props"). The first limits
        were too loose: mf-Treasure_Finders' terrain went in and baked sand with the light of
        the cave under it, leaves went in sharing a tenth of their texels, and 28 world chunks
        of Sardine City changed for nothing. Now: props only, lightmaps up to 128 px, folds
        under 0.1 %, half a point more shared texels at most.
      - `-steps=layoutcheck` lists what a rule change touches before anything is imported: 12
        of the 75 community and unverified maps. Those were imported and baked again and their
        20 cameras compared with the shots from before (`~/.cache/df-bakequality/rule/`):
        mf-Sardine_City (snow heaps smoother), mf-Prehistoric and mf-Treasure_Finders (rocks
        lose their hard light patches), MF-Test_Facility, mf-Space_station, mf-Tropics,
        mf-Worlds_Peak, dm-HolidayTown, and of the Unverified pack mf-castles, MF-SpeedPipes and
        MF-StuntArena (nothing to see at the cameras). mf-City_Rush is left as it was: its one
        mesh, the tower block facade, baked a flat olive from its unwrap and is named `box`.
      - Not done: the Classic pack's maps (they get the rule at their next `rebake`); mf-western's
        logs, which the old comment in `BuildStaticMesh` warns about, were not looked at. The
        shots of 2026-11-16 are 0.2 of 255 brighter all over than the evening before, on maps
        nothing touched too: something in the render, not in the bakes; not found.
      Not tested: High against Medium on the maps of the Classic and Unverified plugins
      (`bakequality.py` reads `Content/Maps` only), GPU Lightmass. Production was measured on
      three maps (thread 16); 4 bounces on three (above).

