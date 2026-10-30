# Gameplay reference (original values)

Sources: decompiled UnrealScript of the retail game (`Engine`, `Rage`, `RageGame`,
`RageWeapons` packages), `System/MobileForces.ini`, `Manual.pdf`, and animation data read
from `RageWeapons.u`. Distances: MF units (uu); in code ×2.25 → cm.

## Player (Pawn / RPlayer / RagePlayer)
| | MF | UE5 |
|---|---|---|
| Collision cylinder | radius 17, half-height 39 | capsule 38.25 × 87.75 |
| Crouched half-height | 20 | 45 |
| Eye height above centre | 27 | 60.75 |
| Run speed (GroundSpeed) | 400 | 900 |
| Acceleration (AccelRate) | 2048 | 4608 |
| Air control | 0.35 | 0.35 |
| Jump (JumpZ) | 325 | 731.25 |
| Step height | 25 | 56.25 |
| Gravity (ZoneGravity) | −950 | −2137.5 (project physics) |
| Ground friction | 8, applied ×2 | GroundFriction 16; braking 8 × factor 2 |
| Walk / crouch speed | ×0.3 (UE1 native) | ×0.3 |
| Swim speed / ladder speed | 200 / 200 | 450 / 450 |
| Health | 100 | 100 |
| FOV | 100 | 100 |
| Fall damage | below −750 uu/s: 100 × ((Vz+750)/(−450))², dead at −1200 | same in cm/s |
| Carry | 6 blocks; speed −5%/block, min 50% | same |
| Headshot | hit Z − centre Z > 0.8 × half-height | same |
| Recoil | AddRecoil(RecoilPower×6); per frame pitch += r×20 rot units, yaw ±r×20, r halves | stepped at 30 Hz |
| Floor checks | flat cylinder bottom | `bUseFlatBaseForFloorChecks` (stands on stair grating rims like MF) |

**Ladders** (LadderClimber): grabbed on touch like MF (the soldier's cylinder meeting the
climber's: falling into it, or walking at the rungs) and put onto the ladder's line at once,
no gliding in. Forward climbs and presses onto the ledge at the top; back climbs down;
reaching the floor walks again; jump lets go (no regrab for 0.5 s).

**Sounds** (RPlayer, Pawn): footsteps every stride while running (RagePlayerSounds stone01-03,
radius 1000; splashes in water; none while walking), jump (jump1), landing thump (Land1) and
grunt on hard landings (land10, PlayLanded), pain by damage (injurL2 / L04 / M04 / H5, 0.3 s
apart), death (deathc*), bullet impacts (WallHitSFX ricochet / imp01 / imp02, flesh
RageChunkHit at 100 uu). One-shot sounds fade out over MF's default 1600 uu.
Weapons (RageWeapons): knife one of five slashes; shotgun FireBoth for both barrels; grenades
pull the pin (teeth or hand) and hum (Powering) while winding up, a timed grenade ticks every
second; rifle hums while charging and chimes at full power (PoweredUp); the HMG deploy plays
tripod / magazine / step back over its 2.5 s (ActivateGun01-03), picking it up 1 s (Pickup);
rockets roar in flight (RocketFly).

**Water** (RageWaterZone, ZoneInfo bWaterZone): a water volume over the zone; swimming at
WaterSpeed 200, Buoyancy 99 / Mass 100 (sinks very slowly). Head under for UnderWaterTime
20 s, then 5 'Drowned' every 2 s; a gasp on coming up. Pain zones (bPainZone, DamagePerSec)
hurt by how deep you stand (feet 0.4, body +0.4, head +0.2) once a second; negative values
heal (mf-City_Rush, mf-Space_station). ZoneVelocity pushes along (BR-thebigisland's current).
Gravity zones (bGravityZone, ZoneGravity) pull soldiers their own way: mf-warehouse's roof vents
are updrafts (-100 against -950 uu/s2: jump in and float up), mf-Prehistoric's push sideways.

**Soldiers** (RageGfx): the helmet soldier (RagePlayerHelmet: `RagePlayer1Mesh`, team A
animations, MFTeamA skins) and the balaclava one (RagePlayerBalaclava: `RagePlayer2Mesh`, team
B set, MFTeamB). Every player is one of the two for now (by player id) until player setup
offers the choice. Skins follow Pawn.SetMultiSkin: elements 0-3 are `<skin>00-03` (blue team,
and everyone without a team) or `04-07` (red); a captain's TeamSkinCaptain element (balaclava 2,
helmet 0) is `08` / `09`. `UDFSoldierAnimInstance` picks animations like Pawn.uc's Get*Anim: by
the weapon's AnimNumber (`ADFWeapon::SoldierAnimSet`, else its InventoryGroup: 1 knife, 2
pistol, 3 machine gun, 4 shotgun, 5 rifle, 6 HMG, 7 launcher, 8 grenades, 9 trip bombs, 10
MG-alike), the Lg (shouldered) poses while zoomed (bRagePointing), Still / Walk (under half
speed) / Run / Backrun / StrafeLeft / StrafeRight by where the soldier moves against where he
faces (±0.75), DuckStill / DuckWalk crouched, Swim / Tread, the Jump pose in the air and Land
on touching down, the knife's stab and grenade / trip bomb throws on firing (guns have no firing
animation: they shoot from the pose they hold), one of Dead2/3/4/5/9B on dying. Changes tween
over 0.1-0.25 s like UE1's TweenAnim. Drivers sit at the wheel (Drive, DriveLeft/Right by the
turn, DriveStill); seats MF marks aSeatsSit get PassLgStill, the others stand. The weapon's
carry mesh is in the right hand (Bone_weapon, MESH WEAPONPOSITION yaw 90 pitch -90). Others
see (and hear) reloads (WalkReload / DuckReload) and a flinch when hurt (GutHit / LeftHit /
RightHit).

**First-person weapons** (RageWeapon PlayerViewMesh): the arms models at the eye, at MF's
PlayerViewScale (0.2; the Deagle 0.04) and PlayerViewOffset (uu: Weapon.setHand's x100 and
CalcDrawOffset's 0.9 / FOV 90 cancel out), squashed to half depth like MF's MESHMAP SCALE 1 /
0.5 / 1 (kept on the mesh as `UDFMeshScaleData`). Animations by MF's names, the first the mesh
has: SelectUp / SelectDown, Fire (or the Fire01-04 / ContinuousFire01-04 / Slash01-04 takes at
random, Shoot, ThrowOver, PlaceForward), AccurateAimFire when zoomed, AltFire / RightThrow,
Reload, AccurateAimUp / Down or BringScopeUp / Down, Still / AccurateAimStill as the idle. Each
plays at the rate that fits the weapon's state time, so MF's timings stay the rule.

**Zooming** (RageWeapon PreZoomIn / PostZoomOut): alt fire raises the sights (AccurateAimUp /
BringScopeUp, ZoomTime), then over ZoomInTime (0.35 s, eased) the FOV narrows to 1 / ZoomScale
and the arms move by the weapon's ZoomedInViewOffset; zooming out runs the other way (view back
over ZoomOutTime, then the sights come down). The arms' offset is scaled by 0.9 / the current
FOV (Inventory.CalcDrawOffset), so zoomed they move out and keep their size: with the
ZoomedInViewOffset that is what lines the sights up with the crosshair. Scopes (bInstantZoom:
sniper rifle, rocket launcher, WA2000) hide the arms and draw the scope mask once raised, not
while the raising animation plays.

## Rules (RageDeathMatch / RageTeamGame)
- **Hardcore mode** (default on): damage from any instigator ×1.5.
- DM: frag limit 30, time limit 7 min. TDM: no frag limit, 12 min, team score per kill,
  friendly fire 0. CTF 4/20, Holdout 6/20, Detonation 4/10, Safe Cracker 1/9,
  Trailer 4/10, Captains 5/12 (frag-or-goal / minutes; to port).
- Respawn after max(1, RespawnWait) s; RespawnWait 0 for DM/TDM, 5 for CTF/Det/SC/Trailer.
- Spawn choice: neutral starts (TeamNumber ≥ 2) in non-team games, own team's starts in team
  games (no team yet counts as team 0, RageTeamGame.FindPlayerStart; never the captain starts
  or the neutral ones, e.g. warehouse's captain's room); random score, −10000 for the last spot
  used, players nearby lower the score.
- Kill +1, suicide −1; first blood; killing sprees at 5 / 10 / 15 / 20 / 25 kills ("is a one
  man killing machine!", "is seriously deranged!", "has gone ballistic!", "is gunning for
  blood!", "is a MOBILE WAR HERO!!!", RageMessageKillingSpree), "'s killing spree was ended by".
- Announcer (Sounds/Announcer): the mode's order a second after joining ("Kill them all",
  "Destroy the enemy", "Get the flag", "Hold the point", "Get the key", "Get the trailer",
  "Get their captain", Safecracker "Crack their safe" / "Defend your safe"), then the time left
  ("You've got N minutes"); "Battle ends in 5 / 3 minutes", "30 seconds left", 10 .. 1; first
  kill, sprees and "Decapitation" only to the player who made them; flag / gold taken and
  returned, "Blue / Red score", "key captured", "Detonation imminent", "Trailer capture",
  "Blue / Red switches"; "Winner" / "You lose" / "Draw" at the end.
- Map end: 15 s (RestartWait) then restart.

## Map items

- Teleporters (Engine.Teleporter): walking into one with a URL puts you at a random teleporter
  Tagged URL (bChangesYaw turns you its way, keeping your angle to the source; speed carried
  over, or TargetVelocity / reversed). mf-Space_station 23, Toreford 15, City_Rush 8 ...
- Items the map placed (weapons, grenades, armour ...) lie there to be picked up and come back
  after their RespawnTime (RageDeathMatch.ShouldRespawn; 30 s by default, mappers' own values and
  MaxClipAmmo / MaxClips kept: Space_station's armour respawns at once). Modes that limit the
  items leave them out; Coop's "weapons on the ground" setting uses them.
- PeriodicSound: ambient loops that play Period s and pause Interval s (birds, wolves, wind).
- Movers (brushes outside the BSP): mostly solid scenery; PHYS_Rotating ones spin at their
  RotationRate (Prehistoric's circling pterodactyls, Treasure_Finders' windmills); doors with key
  frames open when bumped (BumpOpenTimed / StandOpenTimed, MoveTime, StayOpenTime). Like every
  UE1 actor they exist only in the game types GameInfo.IsRelevant allows (bTeamPlay,
  bNonTeamPlay, CondGameType: City_Rush has a blocker per mode).
- Invisible walls: Triggers the mappers made blocking (bBlockPlayers / bBlockActors) stop
  soldiers (and vehicles with bBlockActors) - map edges and roofs on Toreford, Sardine_City,
  Pobojowisko, City_Rush ...
- Spawn room doors (RageGame.LoadoutBlocker, team games): the room's team walks through;
  enemies, captains and flag / key carriers can't, and no projectile or shot gets through.

## Loadout
- Crate trigger radius 129, height 26. DM crates only exist in non-team games, team crates
  and loadout zones only in team games.
- Items (LoadoutCrate.aWeaponsInStore): Pistol, Machine Gun, Shotgun, Sniper Rifle,
  Trip Bombs, Rocket Launcher, Grenades, Adrenaline, Armour, Heavy Machine Gun.
- Entering loadout at a crate / in a loadout zone refills ammo of everything carried.
  Loadout only while walking on the ground.
- In loadout the soldier stands still and can't fire, but stays in the level for everyone
  (seen, solid, hit). Only the spawn loadout protects him: no damage until 5 s after leaving
  it (MF StartupInLoadout); one entered later doesn't. Unlike MF, the mouse still looks
  around (the loadout screen only uses keys, the wheel and Fire).
- **Away from crates and zones the loadout still opens** (MF only opened it there): it lists
  the weapons lying within 562 cm (MF `TryLoadoutDropped`, 250 uu); putting an item back
  drops it on the ground there. At a crate / zone a put-back item goes back into the store.
- Loadout areas don't need closed-off zones (an MF1 limit): place an `LoadoutZone` and
  size its box (`Volume` component) like any trigger volume; set `Team` (255 = everyone).
- Dropped weapons (throw key `;`, death with ammo left) keep their ammo and lie where MF put
  them (`Pawn.DropItem`: 4 collision radii out, random ±71° around the facing, then they fall,
  MF `PHYS_Falling`, until the model's underside rests on the floor; soldiers don't hold them
  up). No touch pickup, like MF: take them through the loadout. 32 per map at most.
- Keys: MF picked an item with its group key (1-9, 0). Groups now hold any number of
  items (mods), so the loadout screen shows **one row per key**: a row with one item is
  picked by its key directly; otherwise the key opens the row and the next number picks
  the item in it (e.g. `8` `2` = Molotov). Group 11 (armour, mod gadgets) had no key in MF;
  it's on **X** here, in loadout and in game. Up/Down + Fire still work.
- HUD layout follows MF: loadout grid top left (bright = carried, dark = fits, grey = no
  room), carry strip bottom centre with each item as wide as its carry blocks plus the free
  blocks, health/armour bottom left.

## Game modes
- **Safecracker** (`sc`, RageSafeCracker): two parts. Part 1: red (FirstAttackerIndex)
  cracks the blue safe (100 health; explosives ×0.15, other damage ×0.05, ≥1 per hit),
  takes the gold and carries it to its own safe within 9 min, else "Blue safeguarded their
  gold!". Part 2 (map reloads with `?Part=2?FirstCaptureTime=N`): sides swap, the new
  attackers must beat part 1's time. Carrier kill +4, steal +7, respawn 5 s. Dropped gold
  goes home after 25 s or when a defender touches it. HUD: safe meter + countdown bottom
  right (RageSafeCrackerHud). Safes/gold are placeholders until the MF meshes convert.
- **CTF** (`ctf`, RageCTF): touch your own flag at home while carrying theirs. 4 captures /
  20 min, carrier kill +4, capture +7, dropped flag home after 25 s or on your team's touch.
- **Detonation** (`det`, RageDetonation): one neutral key at a random key spot; carry it to
  the enemy lock: 5 s countdown, then that team dies one by one (random 0-1 s apart), +7
  and a team point, the key restarts elsewhere. An untouched key moves after 25 s. 4 / 10 min.
- **Captains** (`cap`, RageCaptains): a random captain per team; killing the enemy captain
  is a team point (+7). New captain ~4 s later (at once after a captain's suicide). Captains
  spawn at their CaptainStart (unused as a normal start in every mode, like MF) and are sent
  back there from loadout zones and the start-room corridors (CaptainProhibitedZone →
  `ADFZoneBox` tagged `CaptainProhibited`). 5 points / 12 min. "CAPTAIN" label over them.
- Holdout (Domination), DM, TDM as before; Trailer waits for vehicles (its map actors are
  already in the converted maps).
- **Tug of War** (`tug`, MF-mods TugGame, `Game/Tug`): killing an enemy converts them
  to your team on the spot (full health). A team with nobody left loses the round (+2 to the
  converter); the bigger team wins a round that runs out of time (8 min). 3 rounds; every
  round starts with the original teams and fresh loadouts.
- **Arena** (`arena`, MF-mods arenaMode by Gregaras, `Game/Arena`): rounds without respawning.
  The map's Holdout point is locked for 50 s ("Get ready" at 10, a countdown from 5); then a team
  that stands on it alone for 10 s takes the round; stepping off, or an enemy stepping on, starts
  the count over. Wiping out the other team takes the round too. 5 rounds, 10 min.
- **DeBall** (`deball`, MF-mods MoBall renamed, `Game/DeBall`): football with vehicles. A rubber ball
  (MF `ball.COL`: 1 m radius) appears at the trailer generator; vehicles, soldiers and shots push
  it (by the square root of the damage, shotgun pellets 40 % of the time); into the other team's
  trailer park (192 + 128 uu, 64 up or down) scores, the ball goes 5 s later and a new one comes.
  The goals show as rings of glowing net in the team colour over that whole scoring area (MF
  drew a smaller faint ring, MoBallGoal).
  Loadouts only hand out groups 1, 2, 9, 10, 11 (knife, pistol, gadgets; `bRestrictWeapons`).
  First to 4; a draw after 15 min goes to sudden death. Works on any map with a trailer generator.
- **Battle Royale** (`br`, MF-mods battleRoyale by Gregaras, `Game/BattleRoyale`): free for all,
  no respawning within a round. Everyone drops in from the air (no fall damage on that landing)
  at a random spot on the nav mesh with 500 health and only the default kit (no loadouts). Up to
  200 spots (900 cm apart) get Gregaras' metal towers and, every fourth, an item box: touch it
  for five random weapons. Towers (500 health) drop builder ammo every 50 damage; the builder
  (fire: a platform 300 uu along your view, alt fire: flat / 22.5° ramp / wall, a ghost shows
  where) puts up 150-health platforms, 5 per pickup. A safe zone around the domination timer
  shrinks from 450 m to 11 m (67.5 cm/s); outside costs 5 health a second. The last one standing
  ends the round: new loot, new cars, full zone. Kills score (`bScoreLastedRounds`: survived
  rounds) to 30 in 12 min. Bots go for unopened boxes while poorly armed and back into the zone.
- **Coop mode** (`coop`, MF-mods CoopMode, `Game/Coop`): capture the flag with every human on
  red against a blue team of bots (12 by default, `?Bots=N`); 3 captures, no time limit.
  `?Weapons=N` limits the humans' loadout: 0 none ("Nice try, human!"), 1-2 only weapons on the
  ground, 3 + crates (default), 4 + loadout zones.
- **Zombie Mode** (`zombie`, MF-mods ZombieGame, `Game/Zombie`): humans (team 0) against
  zombies. Bots keep about three humans per zombie. A zombie's boost = (humans per zombie) ^ 0.18
  (1 - 1.253): speed × (min(1.55 boost, 2.2) − 0.2), health 333 × boost, jump × min(1.45 boost, 3),
  falls hurt less, accel / step × 1.15, two carry blocks fewer, run-over damage ÷ 10. Zombies
  spawn at neutral starts away from both bases and carry the zombie knife (67, 95 uu; nemesis
  ×2), reinforced armour (120 charge, 80 %; `?ReinforcedArmour` gives it to everyone) and the adrenaline shot, whose alt fire on an enemy
  with ≤ 100 health makes them change sides (+2, MF ZombieShot); below `?ZombieWeapons=2` they keep nothing ranged, below 3 / 4 no crates
  / zones. Shotguns are the boomstick (`?Boomstick`, on by default here: 12 pellets × 15 per barrel,
  22 spare clips, less kick; MF's separate ZombieShotgun, one blast per button).
  A human killed by a zombie turns (the killer heals up to MaxHealth / 11, less with distance);
  no humans left = zombies take the round, any human alive at the end of the 8 minutes = humans
  take it; 3 rounds, original teams each round. A lone zombie against 5+ humans is a
  **NEMESIS** (boost exponent 0.24, cap 10, regenerates 1.5 / s): infection stops and dead humans
  wait for the next round until it dies. A lone human against 5+ zombies is a **SURVIVOR**
  (health × max(ratio / 2.5, 2), regenerates, clips that never run dry on every weapon with more
  than one use). `?Infect=0`: team deathmatch with zombie bodies.
- Team orders (V / C): Defend! · Defend this area. · Attack! · Cover me. · Improvise!, then
  All or a team-mate. Defend / Attack read per mode (CTF "Guard the flag." / "Capture their
  flag.", Safecracker "Guard the safe." / "Grab the gold."), spoken aloud by whoever gives it
  (RagePlayerVoice, heard nearby; "Improvise" is "Fire at will!" 2 times in 5). Bots acknowledge
  3-4 s later ("Roger that", Rage.int AckString, with the voice), hold the spot, follow, defend
  or attack.

## Weapons
Timing = animation length: (frames − 1) / (seq rate × play rate) + tween.

| Weapon | Group | Carry | Clip × clips | Damage | Spread (Acc / zoomed) | Fire time | Reload | Notes |
|---|---|---|---|---|---|---|---|---|
| Knife | 1 | 0 | — | 30 slash (range 90) | — | ~0.55 s | — | alt: throw, 60 (head 250); sticks in walls, bounces off floors; walk over it to take it back, else back 6 s after it stops |
| Pistol | 2 | 1 | 8 × 6 | 26 (head 40) | 0.4 / 0.05 | 0.342 s | 1.55 s | alt: aim 1.5× |
| Machine gun | 3 | 2 | 48 × 5 | 20 | 0.7 / 0.1 | 0.117 s | 2.25 s | alt: aim 2× (single shots, 0.333 s) |
| Shotgun | 4 | 2 | 2 × 20 | 10 pellets × 12 per barrel | 0.8 / 1.0 | 0.472 s | 0.867 s | fire = one barrel, alt = both (one if only one is loaded; MF: a barrel per button, both buttons within 0.1 s = both); any clip size works; `?Boomstick` 12 × 15, 22 clips; range 5000 |
| Sniper rifle | 5 | 3 | 4 × 4 | 20 × power (1–10), head ×3 | 0.3 / 0 | 0.55 s | 1.383 s | power +2/s; scope 3×–30× (wheel); range 20000 |
| HMG | 6 | 4 | 900 × 1 | 45 | 0.6 / 1.2 | — | — | must deploy (alt, standing on the ground): then no walking, jumping or loadout until packed up (MF PHYS_Gunnery) |
| Rocket launcher | 7 | 4 | 1 × 11 | 135, radius 400 | — | 0.79 s | 3.38 s | rocket 1200 uu/s; alt: scope 4×; scoped, the same enemy (soldier or manned vehicle) within ~22° on two 1.25 s checks locks on (beep; another beep when it's lost) and the next rocket steers at it while it's ahead (SmartRocket) |
| Grenades | 8 | 1 (max 3) | 3 | 160 r400 (timed 3 s) / 120 impact | — | 0.333 s | — | throw power 1.5–10 (+8/s), speed power × 150 uu/s, +210 up |
| Trip bombs | 9 | 1 (max 3) | 2 | — | — | — | — | laser 1024 uu; alt: proximity mine (TODO) |
| Adrenaline | 10 | 1 | 1 | full heal | — | 0.5 s | — | alt: inject team mate (range 90); Zombie mode: an enemy at ≤ 100 health changes sides |
| Armour | 11 | 1 | — | absorbs 75% | — | — | — | 100 charge; falling/drowning bypass |

Mod items (MF-mods, part of the game, in every crate): **Totem of Undying** (group 10:
cheats death once - 5 health, 1.5 s no damage, +19 over 3 s; fire: +56 to you and team-mates
within 400 uu in view; alt: +118 over 10 s to the team-mate you aim at; reload: +120 over 10 s
on yourself), **Trip Laser** (group 9, 3: beam passes team-mates, burns the first enemy 280 /
185 into the open, max 10 out; thrown: 280 on touch), **Seeker Mine** (group 9, 1: thrown
drone hunts the nearest enemy within 1900 uu, 320 r440, escorts you otherwise, max 3),
**Molotov** (group 8), **Desert Eagle** (group 2: 52, head 120, 7 a clip, heavy kick), **Grenade
Launcher** (group 7, carry 4: one round a load, 6 loads; fire an impact grenade 250 in 200 uu, alt
a cluster grenade bursting into 15 fragments of 20), **Predator Missile** (group 11, carry 2:
ready 10 s after you get it; fire launches a missile from the highest free air above you (at
least 400 uu) and you fly it from its nose: the view steers (dive 5°–89°), Fire held boosts to
2.5× (turns slower); 1000 damage in 1400 uu; you stand still while guiding; bots aim it at the
nearest enemy).

**Tracers** (RageWeapon.FireTracer): a streak flies from the gun to where the shot went at
3000 uu/s, every shot for pistols, every 2nd pellet for shotguns, every 5th round for machine
guns (not under 128 uu). **Cases** (EjectBrass): spent cases fly out right and up, bounce up
to three times with a tink and lie there a few seconds.

**The servers' weapon mods** (Gian, Gregaras or Marcin; models, icons and sounds come from the
community pack and the guns are only offered where it's installed; `Weapons/<Name>/`):

| Weapon | Group | Carry | Clip × clips | Damage (head) | Spread (Acc / zoomed) | Fire / zoomed | Reload | Notes |
|---|---|---|---|---|---|---|---|---|
| AK-74 | 3 | 2 | 30 × 8 | 25 (50) | 0.6 / 0.078 | 0.127 / 0.167 s | 1.75 s | aim 2× |
| M4A1 | 3 | 2 | 30 × 8 | 22 (44) | 0.58 / 0.07 | 0.107 / 0.143 s | 2.54 s | aim 2× |
| FAMAS | 3 | 2 | 25 × 10 | 28 (56) | 0.58 / 0.07 | 0.11 / 0.167 s | 3.15 s | aim 2× |
| G36 | 3 | 2 | 30 × 8 | 24 (48) | 0.6 / 0.07 | 0.114 / 0.167 s | 3.05 s | aim 2× |
| SC-20K (F2000) | 6 | 3 | 35 × 8 | 28 (70) | 0.545 / 0.056 | 0.103 / 0.133 s | 3.25 s | aim 2× |
| Ultimax 100 | 6 | 4 | 100 × 4 | 30 (60) | 0.65 / 0.07 | 0.112 / 0.45 s | 3.63 s | the "assault HMG" |
| MAC-10 | 2 | 1 | 30 × 8 | 22 (44) | 0.76 / 0.1 | 0.086 / 0.125 s | 1.38 s | |
| Glock 18 | 2 | 1 | 30 × 4 | 28 (40) | 0.05 / 0.5 | 0.283 / 0.097 s | 2.79 s | aiming (no zoom) turns it into a wild full-auto |
| Desert Eagle | 2 | 1 | 12 × 5 | 25 (70) | 0.22 / 0.05 | 0.3 / 0.231 s | 2.68 s | aim 1.5× |
| M24 | 5 | 3 | 8 × 6 | 80 (180) | 0.1 / 0.05 | 1.65 / 1.86 s | 3.18 s | bolt action, aim 2× |
| WA 2000 | 5 | 3 | 6 × 5 | 60 (head: dead) | 0.3 / 0 | 0.61 / 0.56 s | 3.68 s | scope 6× (MF 3×–30×) |
| M1887 | 4 | 3 | 8 × 8 | 12 pellets × 14 (28) | 1.3 / 0.7 | 1.19 s | 2.25 s | lever-action "long shotgun" |
| M18 smoke | 8 | 1 | 3 | — | — | 0.333 s | — | a grenade that pours dense smoke for 15 s where its fuse runs out |

Base trace: start at the eye, end = start + Acc×(rand−0.5)×1000·Y + Acc×(rand−0.5)×1000·Z
+ MaxTraceDistance (10000)·X.

**Parachutes** (Gregaras, the community servers' mutPack bCanUseParachutes): hold Jump while
falling faster than a normal jump can return (jump speed + 100 cm/s) and a canopy opens; the fall slows to 200 uu/s and you
glide the way you look. Let go of Jump and it folds. `?Parachutes=0` turns them off.
Entering any vehicle seat closes it on the server and on the owning client.

**Minefields** (mineGenerator.u, mf-Pobojowisko_VerII): the map's mines sit on the ground,
nobody's; a soldier within 64 uu or a vehicle within 96 uu sets one off (120 damage in 200 uu,
a fountain of dirt) and a new one is laid 5 s later. The river's splashMakers just splash.

## Vehicles
Physics box + raycast wheels (`ADFVehicle`), numbers from MF's own data: `Physics/handling.hnd2`
(mass, MaxSpeed in mph, steering lock), `Physics/<Vehicle>.COL` (wheel spheres in metres, x right /
y up / z forward = UE Y / Z / X), Rage classes (seats, doors, NearCamera, Armour; uu x 2.25).

| | Mass | Top speed | Drive | Armour | Seats |
|---|---|---|---|---|---|
| Buggy | 800 kg | 70 mph | rear | 0.367 | 2 |
| Humvee | 2000 kg | 50 mph | 4x4 | 0.68 | 4 |
| Truck (6 wheels) | 3000 kg | 50 mph | all | 0.78 | 5 |
| Armadillo APC | 2300 kg | 30 mph | 4x4 | 0.84 | 2 |

- E (TryEnterExit) near a vehicle selects the nearest free seat. Paired front seats use an
  empty driver seat, matching MF aSeatNearDrivers; rear seats stay passengers. Right Alt
  (`UseClosest`) uses the nearest free seat including the driver, without front-seat redirection.
  It also selects loadout items, so it can replace E through Controls.
  E again exits through a clear door. Drivers drive (WASD, Space
  handbrake, mouse orbits the camera) and can't shoot; passengers ride standing or sitting at
  their seat, look around and use their own weapons (MF InCarState).
- Bots (RageBot): 4 in 5 use vehicles in deathmatch, up to 3 in 4 of a team's bots in team
  games. A trip of more than 10 road nodes (RoadPoints) sends a bot to the most wanted free
  vehicle within 56 m (Vehicle.BotDesireability: 1.1, −0.3 a flat tyre, ±0.5 / ±0.2 for a team
  mate / enemy at the wheel / in a seat; nearer and longer trips weigh more). It drives MF's road
  graph at each RoadPoint's MaxSpeed, steers round parked cars, backs out when stuck (after six
  tries the vehicle counts as stuck for 30 s) and gets out at the node nearest its goal, in water
  or on its roof. Seated bots fight from the seat (the gunner aims the turret); they get out once
  the driver has gone and it stopped. Bots told to follow get in with you. In DeBall a bot at
  the wheel never gets out: it drives at the ball, straight when it can see it, else along the
  road graph towards it (MoBallGame.FindSpecialAttractionForX).
- **Armed APC** (MF-mods ArmedAPC, `?APCs=ArmedAPC`, rockets `?APCs=ArmedAPCRocket`, at the Armadillo generators): the
  gunner seat (the hatch) runs a heavy machine gun aimed with the gunner's view (pitch −11° … 49°,
  165°/s), Fire shoots: 100 a belt, 10/s, 25 damage, 13500 cm, 4 s reload (R swaps early). The
  rocket version: 4 rockets, 0.8 s apart, 110°/s.
- **TurboCars** (MF-mods TurboCars, on by default like the community servers' mutPack
  replaceVehicles; `?TurboCars=0` off): drivers hold Crouch (Ctrl / C) for nitro, pushing
  forward on the ground up to the vehicle's nitro top speed (Buggy 50 m/s, Humvee 40,
  Armadillo 50, Truck 40 (TurboTruck); trailer cabs never had it). Nitro drives like the
  throttle: steering works the same with it alone.
- Hits (MF Vehicle / Physical.TakeDamage): every hit clangs (MiscSFX.HitMetal) and shoves the
  car by √momentum (knife slashes and bullets a little, rockets a lot; pellets 40 % of the
  time). Bullets and knives hit the model itself, not a box around it. Soldiers can't push cars
  by walking into or standing on them.
- Tyres (MF WheelCollision): 35 damage of anything bursts one (HitRubber, a jolt): it runs on
  the rim - lower, a third of the grip, half the drive, so a flat front tyre steers badly - and
  re-inflates after 30 s (ReInflatePause). Explosions reach the tyres too. Tyres block pawn
  movement queries; ground support follows the contact normal so a parked flat does not
  create sideways thrust.
- Petrol cap (MF PetrolCap at PetrolCapPos, 7 uu - trucks 12): 150 damage of bullets or pellets
  there and the vehicle blows up. A small target: aim at the tank.
- Damage smoke grows denser and darker as health falls. It uses a bounded local emitter;
  replicated health drives its rate and opacity on each client.
- Blowing up (EffectsFix.VehExplosion -> RageEffects.VehicleExplosion): a fireball of smoke and
  flames, 2-4 fires on the ground around it, and 16 chunks (RageEffects.VehicleFrag carfrag01-09)
  that spin, bounce with half their speed and clank (FragHit1-3), then lie there 10-17 s; up to
  4 of them burn (ExplosionFire: flames and smoke for 4.5 s, VehicleBurnLoop). A blown tyre puffs
  smoke (SmokePuffVeh). Tyres landing hard thump
  (Wheel.HitWall, HitWheel); bumping into things clunks (Bump1-4).
- Damage: explosives x 1.5 x (1 - Armour) (Vehicle.TakeDamage, Physical); MF ignores bullets,
  pellets and knives, but the community servers ran mutPack's **changeDamage**, the rules
  here: bullets / pellets x 0.125 x (1 - Armour) (10 / 80
  against 8000 health), knives nothing, bots do a fifth to vehicles and to players riding in
  them, and players' rockets hit soldiers twice as hard. 100 health; at 0 it blows
  up (150, 900 cm) and kills everyone inside. Hitting someone at KillSpeed 8 m/s or more runs
  them over. Pawn contacts apply a small one-time speed loss rather than a rigid-body stop;
  a separate query shell keeps walking soldiers outside the body. Upside down and slow
  for 3 s: controlled torque rolls the body upright through normal floor contact. Tyre traction
  retains 35% of its overturning lever arm; suspension forces still act at the contact points.
- Exiting keeps collision disabled until the soldier is outside and possession is restored.
  For one second, that vehicle cannot run over its exiting occupant; other damage still applies.
  Drivers remain hittable through mesh openings, as passengers do.
- Empty generated vehicles recycle after 45 seconds if moved more than 450 cm horizontally
  or damaged. Occupying the vehicle cancels the timer. This includes deliberate passengers;
  pristine vehicles at their generator remain available. Locked objective vehicles are excluded.
  During the last 30 seconds, a red overlay pulses increasingly quickly. Expiry plays a smoke
  puff and fragment sound with low-energy debris that lasts three seconds; it causes no blast damage.
- Generators: the map's Buggy/Humvee/Truck/ArmadilloGenerator actors make one each and a new one
  5 s after it's gone; VehicleGenerators with a VehicleClass too (boat.boat, jeep.jeep,
  Rage.*). `?Vehicles=0` turns them off.
- Water: vehicles float on the map's water plane (PhysicalWaterInfo.WaterHeight) with their
  handling.hnd2 Buoyancy (65: cars float two thirds under; boat 50) and water drag. Maps
  without a PhysicalWaterInfo have none: cars drive along the bottom, as in MF.
- **Boat** (MF-mods boat, Gregaras; CC0 model): only drives afloat - keel in the water, deck
  out (MF waterCollider test). Throttle ramps 0.004 a tick to 0.85 (MF), top 0.85 x 12 m/s,
  reverse 0.2 x 12; steering spins it (0.05 a tick), reversed when backing. Three seats.
- **Jeep** (MF-mods jeep: model Marcin, code Gregaras): 1000 kg 4x4, 60 mph (handling JEEP,
  jeep.COL), two front seats and a gunner standing at the back on a machine gun (3000
  round belt, MachineGun damage, aimed with the gunner's view). `?Trucks=Jeep`: truck generators
  make jeeps (the servers' mutPack placed them per map).
- **Heli** (MF-mods heli, Gregaras): 1500 kg, flies by hovering: the rotor spins up for about
  5 s after the pilot gets in, then Jump climbs, Crouch sinks, forward / back / strafe tilt it
  and push it along (MF hover forces, lift scaled to the world's gravity). Four glass panes
  (300 hit points each, back after 20 s) crack and shatter. Bots ride along with a player at
  the controls but don't fly. **HeliHmg**: two side gunners on machine guns (jeep gun, 90°
  either way). On the servers mutPack's replaceVehicles turned trucks into helis: `?Trucks=Heli`
  does that (default: trucks).
- **Piper** (MF-mods piper, Gregaras; one or two seats, three liveries): drives like a car to
  50 mph (propeller pull, no rolling resistance); Jump with throttle past 10 m/s lifts off.
  Airborne it holds its airspeed (up to 30 m/s, slowing in steep climbs, picking up in dives,
  stalling below 15 m/s flat / 10 m/s climbing), Jump / Crouch pitch, steering rolls and
  yaws, Fire / AltFire roll alone. Touching down on three wheels lands it, anything else
  crashes. No bots fly it.
- **Added vehicles** (mutPack addVehicles): the servers' extra vehicles (pipers, helis, boats,
  jeeps) are part of the maps: `Tools/mfport/map_edits.toml` places their generators when a map
  is converted, and swaps a map's own (BR-thebigisland's truck is a heli, as the servers' heli
  mod made it). Server admins can still add or swap vehicles per map without converting again:
  `+AddedVehicles=(Map=...,VehicleClass=...,Location=(...),Yaw=...)` and
  `+VehicleSwaps=(Map=...,From=...,To=...)` under `[/Script/DesktopForces.DFGameSettings]`
  (`?AddVehicles=0` turns the added ones off).
- **Trailer** (`trailer`, TrailerGame): a neutral cab (Humvee) towing a bomb trailer (ball-joint
  hitch) starts at the TrailerGenerator. Drive it into the enemy's delivery bay (TrailerDelivery,
  128 + 128 uu): it locks, "Trailer detonation in 5 sec", blows up (600, 1200 uu), team point and
  +7 for the driver; a new one 5 s later (also when it's destroyed on the way). 4 / 10 min.
  Vehicle generators at the bays stay off in this mode.

**Score limit**: `?ScoreLimit=N` is the single match target (0 disables it). Its unit
follows the mode: kills/points, captures, rounds, or Holdout minutes. There are no old-name
aliases. Deathmatch defaults to 30; team deathmatch retains its time-only default.

**Crosshairs**: shape cycling omits the smaller D/F/H variants of E/G/N. Existing texture
IDs remain valid; a separate 25–200% size option scales the selected shape and its preview.
Visible alpha bounds are cropped before drawing, so 100% has the same outer size for every
shape, including the dot. The other ring/line combinations retain their different geometry.
Mouse sensitivity and inversion are under Controls; crosshair shape/size and HUD size stay
together under Game. Weapon position changes only the first-person model: Left mirrors it,
Center uses MF's classic centered offsets, Hidden hides it. Aim and hit traces are unchanged.

**Spawn fade**: the fallback black wait is 1 second, the possession wait about 0.067 seconds,
and the fade 0.2 seconds, each reduced by one third.

**Audio**: Master controls the whole mix. Music, Sound effects, and Announcer have independent
volume controls; effects include weapons, vehicles, ambient loops and interface sounds.
Changing effects volume also affects already-playing loops through their sound class.

**Scorch marks**: Simple uses flat instanced quads. Full projects a 16×16 mesh onto nearby
static surfaces, with gaps rejected; this follows slopes/steps without DBuffer. Bullet holes
stay lightweight quads. Full is capped at 24 scorch meshes, each lasting 30 seconds.

### Feign death

Left Alt toggles `FeignDeath`; the binding can be changed in Controls. It requires a living,
grounded soldier outside vehicles and loadout. The soldier lowers into a damageable crouch
capsule, holds MF's DeathEnd pose, hides the weapon, and drops a carried flag. Movement and
firing are blocked, but damage and gravity remain active. Bots stop selecting the apparent
corpse as a new target. Alt, Fire, Alt Fire, or Jump gets up after the initial 0.7-second pose;
the wake-up press does not also shoot or jump, and the weapon uses its normal equip delay.

### Player preferences

Options > Game offers Auto, Helmet, and Balaclava models. Model changes replicate to other
players. Preferred team applies on joining a match and remains subject to mode and balance
rules. Auto preserves automatic assignment. The classic loadout now shows each owned weapon
instance’s live ammunition rather than the shop’s default quantities.

Truck turbo uses the existing server `TurboCars` option and nitro input, like other eligible
vehicles. Vehicle flags use the original MF team emblems on a two-sided masked material.
