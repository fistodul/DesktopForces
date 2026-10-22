# Architecture

Two C++ modules in one UE project (`DesktopForces.uproject`), a content-only plugin with the
original game's converted content, and the Python pipeline.

| Part | What | Committed |
|---|---|---|
| `Source/DesktopForces` | the game (incl. the MF-mods, filed under `Game/`, `Weapons/`, `Vehicles/`) | yes |
| `Source/DesktopForcesEditor` | importer commandlet | yes |
| `Content/MF` | our assets: master materials, menu art, crate, test map | yes (music: generated) |
| `Content/Maps`, `Content/MF/Community` | community maps cleared for the game ([packs](Pipeline.md#content-packs)) | generated |
| `Plugins/MobileForcesClassic` | stock maps + all original textures, sounds, music, fonts, models (`/MobileForcesClassic/...`) | generated, never |

Game code reaches classic content by path (`/MobileForcesClassic/...`, helper `DF::LoadClassic`
in `Core/DFContent.h`) and copes when it's missing.

The project was called `MobileForces` until 2026-10-20; `[CoreRedirects]` in
`DefaultEngine.ini` keep old assets loading.

**Names** (2026-10-22): the framework's classes carry `DF` (`ADFCharacter`, `ADFGameMode`,
`ADFWeapon`, `UDFUserSettings`, namespace `DF::`, log `LogDF`); what you meet in the game has a
plain name - weapons (`AKnife`, `ARocketLauncher`), vehicles (`ABuggy`, `AHeli`), projectiles
and items (`ARocket`, `ASeekerMine`), game objects (`AFlag`, `ASafe`) and modes (`ADeathMatch`,
`ACaptureTheFlag`, `AZombieGame`) - so `summon Buggy`, `GiveWeapon Knife`, `?game=ctf` (short
names: `GameModeClassAliases`). The old `MF` names redirect (`[CoreRedirects]`).

## `DesktopForces` (runtime game module)

| Folder | Class | Original MF class(es) | Role |
|---|---|---|---|
| `Core/` | `DFTypes.h` | `Pawn.Team`, damage type names | team ids are `uint8` (`DF::NoTeam` = 255; also `FGenericTeamId` for AI perception), `UDFDamageType` subclasses (`Bullet` = 'tracedshot', `Shotgun` = 'scattershot', `Knife` = 'ArmourPiercing', `Headshot` = 'Decapitated', `Explosion`, `Fell`, `Drowned`, `Crushed`, `Suicide`) |
| `Core/` | `UDFGameSettings` | `RageTeamGame.TeamNames`, `RageBotA..P`, `LoadoutCrate.aWeaponsInStore` | `UDeveloperSettings` in `DefaultGame.ini`: any number of teams (name, colour), loadout list, bot roster (32 MF bots), team tint material |
| `Core/` | `UDFGameInstance` | — | game-wide state; scripted smoke tests (`-DFExec`, `-DFAutoShot`, `-DFQuitAfter`) |
| `Player/` | `ADFCharacter` | `Pawn` > `PlayerPawn` > `RPlayer` > `RagePlayer` | health, armour via inventory, carry blocks, inventory & weapon switching, fall damage, recoil, loadout visibility |
| `Player/` | `UDFCharacterMovement` | UE1 `PHYS_Walking` / `PHYS_Ladder` | MF movement constants, carry/aim/walk speed scaling, ladder custom mode, walk flag in saved moves |
| `Player/` | `ADFPlayerController` | `RagePlayer` input + loadout states | Enhanced Input built in code (original key map), loadout menu flow, weapon cycling, scoreboard; console commands `DFLoadoutPick N`, `DFLoadout`, `DFSelect N`, `DFScores`, `DFShot name` |
| `Player/` | `UDFCheatManager` | — | debug commands: `DFNavTest` (path report), `DFTopView X Y Width` (ortho camera) |
| `Weapons/` | `ADFWeapon` | `Weapon` > `RageWeapon` | ammo (clip + spare clips), state machine (equip/idle/fire/reload/zoom/put down), MF `Finish()` logic, hitscan `TraceFire`, projectiles, replication |
| `Weapons/<Name>/` | `A<Name>` (`<Name>.h/.cpp`, e.g. `AKnife`, `ARocketLauncher`; stock ones via `DFWeapons.h`) | `RageKnife`, `Pistol`, `MachineGun`, `shotgun`, `RageRifle`, `RocketLauncher`, `Grenades`, `AdrenalineShot`, `RageArmour` | per-weapon numbers and special behaviour (twin barrels, charge shots, throw power, injecting, armour absorption) |
| `Weapons/` | `ARocket`, `AGrenade`, `AThrownKnife` ... (base `ADFProjectile`) | `Rocket`, `Grenade1`, `Grenade2`, `Knife_Thrown` | `UProjectileMovementComponent`, MF HurtRadius (linear falloff) |
| `Loadout/` | `ADFLoadoutSource`, `ALoadoutCrate`, `ALoadoutZone` | `LoadoutCrate*`, `LoadoutZone` | where players may open loadout; item list; team / game-type relevance |
| `Game/` | `ADFGameMode` (+`_DM`, `_TDM`) | `RageGameInfo` > `RageDeathMatch` > `RageTeamGame` | on `AGameMode` (UE match states): N teams (`?Teams=`), spawning (`FindPlayerStart`), bots (`?Bots=`), damage modifiers, scoring, match end, respawn, `GetBotGoals` |
| `Game/KOH/`, `Game/Holdout/` | `AKingOfTheHill`, `AHoldoutPoint`, `AHoldout` | `RageDomination`, `RageDominationTimer`, MF-mods KOH | capture points: KOH is the base (several points, per-second scoring), Holdout one central point, Arena on top of Holdout |
| `Game/<Mode>/` | `ACaptureTheFlag`, `_Captains`, `_Detonation`, `_Safecracker`, `_Trailer`, ... | the Rage game types and MF-mods ones | one folder per game mode, MF's own and the mods' alike |
| `Vehicles/<Name>/` | `A<Name>` (`ABuggy`, `AHeli` ...; stock ones via `DFVehicles.h`) | `Buggy`, `Humvee`, `Truck`, `Armadillo`, `TrailerCab` / `TrailerX`, MF-mods jeep, boat, heli, piper | one folder per vehicle; the base `ADFVehicle` (raycast wheels, seats, damage) in `Vehicles/` |
| `Game/` | `ADFGameState`, `ADFPlayerState`, `ADFPlayerStart` | `RageGameRepInfo`, `PlayerReplicationInfo`, `PlayerStart` | on `AGameState` (`ElapsedTime`); team scores array; team/frags/deaths; team spawn points |
| `Game/` | `UDFDeathMessage`, `UDFAnnouncement` | `RageDeathMessage`, `RPlayer.spreenote` | `ULocalMessage`s: kill feed, first blood, sprees, captures, match end |
| `UI/` | `ADFHUD` | `RageHUD` | Canvas HUD: crosshair/scope, health/armour/ammo, weapon strip, clock, kill feed, loadout menu, scoreboard |
| `World/` | `ADFMapInfo`, `ADFLegacyActor` | `LevelInfo`, everything not ported yet | map title/music; preserved original actors (class, team, raw properties) for future game modes |
| `World/` | `ADFLadderVolume` | `LadderClimber` | ladder movement trigger + two-way nav link (ends placed by the level builder) |
| `World/` | `ADFZoneVolume` | `ZoneInfo` water / pain / `ZoneVelocity` / `ZoneGravity` | physics volume over a UE1 zone's bounds: swimming, drowning, pain / healing, currents, its own gravity (`GetGravityZ`) |
| `World/` | `ATeleporter`, `ADFPeriodicSound`, `ADFPickupSpot` | `Teleporter`, `PeriodicSound`, `Inventory` placed in maps | teleporters (URL -> Tag), ambient loops on a period, items lying around that respawn after their RespawnTime |
| `World/` | `ADFMover`, `ADFBlocker` | `Mover`, blocking `Trigger`s, `LoadoutBlocker` | brushes outside the BSP (scenery, spinners, bump doors; GameInfo.IsRelevant per mode), invisible walls, team spawn-room doors |
| `World/` | `ADFRoadNetwork` | `RoadPoint`, `ReachSpec` with `R_ROAD` | the map's road graph (MF's reach specs, read from the `.umf`), A* routes for driving bots, `RoadPoint.MaxSpeed` per node |
| `AI/` | `ADFBotController` | `RageBot` | 5 Hz think: loadout, fight (skill-based aim), objectives from the game mode, roam; remembers unreachable goals; takes vehicles for long trips (`PickVehcicle` weights) and drives the road graph, rides and shoots as a passenger |
| `Vehicles/` | `ADFVehicle` (+`_Buggy`, `_Humvee`, `_Truck`, `_Armadillo`, `_TrailerCab`, `_Trailer`), `AVehicleGenerator` | `RageVehicle`s, `*Generator` | physics box + raycast wheels (springs use the world's gravity: MF's is 2.2x UE's), handling from `handling.hnd2` / `.COL` |
| `Game/` | `ASafecracker`, `_CTF`, `_Detonation`, `_Captains`, `_Trailer` | the MF team modes | see [Gameplay](Gameplay.md) |
| `UI/` | `DFMenu` (Slate), `ADFMenuGameMode` | `RageMenu` | title screen over a picture (`UDFGameSettings::MenuBackgrounds`, start-up music `MenuMusic`), play / join / options (render scale, brightness ...) / credits, Esc menu in game |
| `Game/<Mode>/`, `Weapons/<Name>/`, `Vehicles/<Name>/` | `AKingOfTheHill`, `ATotem`, `AArmedAPC`, ... | MF-mods | the community mods, one folder each, see [Modding](Modding.md) |

### Networking
Server-authoritative, UE replication from day one (listen and dedicated servers).
- Movement: `UCharacterMovementComponent` prediction; the walk flag rides in compressed flags.
- Weapons: client input → `Server*` RPCs; server runs the weapon state machine and applies
  damage; the owning client runs the same state machine for instant feedback; other
  clients play effects on `BurstCounter` changes. Ammo replicates to the owner only.
- Loadout: client only *displays* the menu (`ADFHUD`), picks go through `ServerLoadoutPick`.

### Adding a weapon
1. Subclass `ADFWeapon` (or an existing weapon) in a new folder, `Weapons/<Name>/<Name>.h/.cpp` (class `A<Name>`)
   (every weapon has its own folder, MF's and the mods' alike; stock ones are listed in
   `DFWeapons.h`); set the MF numbers in the constructor with a comment naming the original
   value. Set `HUDIcon`/`HUDIconRect`.
2. Override `FireShot`/`TryFire`/`StartFire` only for behaviour that differs.
3. Stock weapons: add to `LoadoutItems` in `Config/DefaultGame.ini`. Mod weapons: set
   `bAddToLoadout = true` (see [Modding.md](Modding.md)).
Blueprint subclasses work too for tuning (all numbers are `EditDefaultsOnly`).

### Adding a game mode
Subclass the closest mode (`ATeamDeathMatch`, `AHoldout`): override `ScoreKill`,
`CheckEndConditions`, `IsStartAllowed`, `OnTimeLimit`, `GetBotGoals`. Objectives from
converted maps are `ADFLegacyActor`s: `ADFLegacyActor::FindAll(World, "RageFlagBase")` gives
the flag bases with their `Team` and original properties — spawn real actors from them at
match start (Holdout does this with `RageDominationTimer`). The MF-mods modes (KOH, Tug) are
examples.

### Design rules
- Shallow hierarchies: one stock class per concept, variations through properties,
  config or components — not MF-style chains (`PlayerPawn > RagePlayerBase > RPlayer > ...`).
- No fixed limits: teams, bots, loadout items and capture points are arrays.
- Engine first: UE match states, `ULocalMessage`, `UDeveloperSettings`, `FGenericTeamId`,
  `MaxWalkSpeed`, navmesh, plugins. Port MF behaviour only where the engine has no equivalent.
- One file per class where classes are big enough to have their own behaviour (weapons,
  modes, vehicles); tiny data-only subclasses may share a file.

## `DesktopForcesEditor` (editor module)
- `UDFImportCommandlet` (`-run=DFImport`): runs the steps below.
- `FDFImporter`: textures (AssetTools import + mobile settings), sounds, master materials
  (`/Game/MF/Materials/M_MF_*`, created in code), material instances per texture+mode,
  static meshes from `.mfmesh` (MeshDescription, lightmap UVs, collision), our own art
  (`ImportArt`: `Tools/artgen/out` -> `/Game/MF/...`), and `BuildMap()` (`DFLevelBuilder.cpp`)
  which spawns the `level.json` records into a new level, places ladder nav links and bakes the
  navmesh. Every converted asset goes under the root of its content pack (`FDFPacks`, from
  `work/out/packs.json`).

## `Tools/mfport` (Python)
See [Pipeline.md](Pipeline.md). MF-specific knowledge lives here (actor mapping, light
colour formula, poly flags); the C++ importer stays generic.
