# Architecture

Two C++ modules in one UE project (`DesktopForces.uproject`), a content-only plugin with the
original game's converted content, and the Python pipeline.

| Part | What | Committed |
|---|---|---|
| `Source/DesktopForces` | the game (incl. the MF-mods in `Mods/`) | yes |
| `Source/DesktopForcesEditor` | importer commandlet | yes |
| `Content/MF` | our assets: master materials, menu art, crate, test map | yes (music: generated) |
| `Content/Maps`, `Content/MF/Community` | community maps cleared for the game ([packs](Pipeline.md#content-packs)) | generated |
| `Plugins/MobileForcesClassic` | stock maps + all original textures, sounds, music, fonts, models (`/MobileForcesClassic/...`) | generated, never |

Game code reaches classic content by path (`/MobileForcesClassic/...`, helper `MF::LoadClassic`
in `Core/MFContent.h`) and copes when it's missing.

The project was called `MobileForces` until 2026-10-20; `[CoreRedirects]` in
`DefaultEngine.ini` keep old assets loading. Class prefixes stayed `MF`.

## `DesktopForces` (runtime game module)

| Folder | Class | Original MF class(es) | Role |
|---|---|---|---|
| `Core/` | `MFTypes.h` | `Pawn.Team`, damage type names | team ids are `uint8` (`MF::NoTeam` = 255; also `FGenericTeamId` for AI perception), `UMFDamageType` subclasses (`Bullet` = 'tracedshot', `Shotgun` = 'scattershot', `Knife` = 'ArmourPiercing', `Headshot` = 'Decapitated', `Explosion`, `Fell`, `Drowned`, `Crushed`, `Suicide`) |
| `Core/` | `UMFGameSettings` | `RageTeamGame.TeamNames`, `RageBotA..P`, `LoadoutCrate.aWeaponsInStore` | `UDeveloperSettings` in `DefaultGame.ini`: any number of teams (name, colour), loadout list, bot roster (32 MF bots), team tint material |
| `Core/` | `UMFGameInstance` | — | game-wide state; scripted smoke tests (`-MFExec`, `-MFAutoShot`, `-MFQuitAfter`) |
| `Player/` | `AMFCharacter` | `Pawn` > `PlayerPawn` > `RPlayer` > `RagePlayer` | health, armour via inventory, carry blocks, inventory & weapon switching, fall damage, recoil, loadout visibility |
| `Player/` | `UMFCharacterMovement` | UE1 `PHYS_Walking` / `PHYS_Ladder` | MF movement constants, carry/aim/walk speed scaling, ladder custom mode, walk flag in saved moves |
| `Player/` | `AMFPlayerController` | `RagePlayer` input + loadout states | Enhanced Input built in code (original key map), loadout menu flow, weapon cycling, scoreboard; console commands `MFLoadoutPick N`, `MFLoadout`, `MFSelect N`, `MFScores`, `MFShot name` |
| `Player/` | `UMFCheatManager` | — | debug commands: `MFNavTest` (path report), `MFTopView X Y Width` (ortho camera) |
| `Weapons/` | `AMFWeapon` | `Weapon` > `RageWeapon` | ammo (clip + spare clips), state machine (equip/idle/fire/reload/zoom/put down), MF `Finish()` logic, hitscan `TraceFire`, projectiles, replication |
| `Weapons/` | `AMFWeapon_<Name>` (`MFWeapon_<Name>.h/.cpp`, all via `MFWeapons.h`) | `RageKnife`, `Pistol`, `MachineGun`, `shotgun`, `RageRifle`, `RocketLauncher`, `Grenades`, `AdrenalineShot`, `RageArmour` | per-weapon numbers and special behaviour (twin barrels, charge shots, throw power, injecting, armour absorption) |
| `Weapons/` | `AMFProjectile_*` | `Rocket`, `Grenade1`, `Grenade2`, `Knife_Thrown` | `UProjectileMovementComponent`, MF HurtRadius (linear falloff) |
| `Loadout/` | `AMFLoadoutSource`, `AMFLoadoutCrate`, `AMFLoadoutZone` | `LoadoutCrate*`, `LoadoutZone` | where players may open loadout; item list; team / game-type relevance |
| `Game/` | `AMFGameMode` (+`_DM`, `_TDM`) | `RageGameInfo` > `RageDeathMatch` > `RageTeamGame` | on `AGameMode` (UE match states): N teams (`?Teams=`), spawning (`FindPlayerStart`), bots (`?Bots=`), damage modifiers, scoring, match end, respawn, `GetBotGoals` |
| `Game/` | `AMFGameMode_Holdout`, `AMFHoldoutPoint` | `RageDomination`, `RageDominationTimer` | capture points; hold time per team; one point per map by default, subclasses spawn more (KOH plugin) |
| `Game/` | `AMFGameState`, `AMFPlayerState`, `AMFPlayerStart` | `RageGameRepInfo`, `PlayerReplicationInfo`, `PlayerStart` | on `AGameState` (`ElapsedTime`); team scores array; team/frags/deaths; team spawn points |
| `Game/` | `UMFDeathMessage`, `UMFAnnouncement` | `RageDeathMessage`, `RPlayer.spreenote` | `ULocalMessage`s: kill feed, first blood, sprees, captures, match end |
| `UI/` | `AMFHUD` | `RageHUD` | Canvas HUD: crosshair/scope, health/armour/ammo, weapon strip, clock, kill feed, loadout menu, scoreboard |
| `World/` | `AMFMapInfo`, `AMFLegacyActor` | `LevelInfo`, everything not ported yet | map title/music; preserved original actors (class, team, raw properties) for future game modes |
| `World/` | `AMFLadderVolume` | `LadderClimber` | ladder movement trigger + two-way nav link (ends placed by the level builder) |
| `AI/` | `AMFBotController` | `RageBot` | 5 Hz think: loadout, fight (skill-based aim), objectives from the game mode, roam; remembers unreachable goals |
| `Vehicles/` | `AMFVehicle` (+`_Buggy`, `_Humvee`, `_Truck`, `_Armadillo`, `_TrailerCab`, `_Trailer`), `AMFVehicleGenerator` | `RageVehicle`s, `*Generator` | physics box + raycast wheels (springs use the world's gravity: MF's is 2.2x UE's), handling from `handling.hnd2` / `.COL` |
| `Game/` | `AMFGameMode_Safecracker`, `_CTF`, `_Detonation`, `_Captains`, `_Trailer` | the MF team modes | see [Gameplay](Gameplay.md) |
| `UI/` | `MFMenu` (Slate), `AMFMenuGameMode` | `RageMenu` | title screen over a picture (`UMFGameSettings::MenuBackgrounds`, start-up music `MenuMusic`), play / join / options (render scale, brightness ...) / credits, Esc menu in game |
| `Mods/<Name>/` | `AMFWeapon_Totem`, `AMFGameMode_KOH`, ... | MF-mods | the community mods, see [Modding](Modding.md) |

### Networking
Server-authoritative, UE replication from day one (listen and dedicated servers).
- Movement: `UCharacterMovementComponent` prediction; the walk flag rides in compressed flags.
- Weapons: client input → `Server*` RPCs; server runs the weapon state machine and applies
  damage; the owning client runs the same state machine for instant feedback; other
  clients play effects on `BurstCounter` changes. Ammo replicates to the owner only.
- Loadout: client only *displays* the menu (`AMFHUD`), picks go through `ServerLoadoutPick`.

### Adding a weapon
1. Subclass `AMFWeapon` (or an existing weapon) in a new `Weapons/MFWeapon_<Name>.h/.cpp`
   (add it to `MFWeapons.h`); set the MF numbers in the constructor with a comment naming the
   original value. Set `HUDIcon`/`HUDIconRect`.
2. Override `FireShot`/`TryFire`/`StartFire` only for behaviour that differs.
3. Stock weapons: add to `LoadoutItems` in `Config/DefaultGame.ini`. Mod weapons: set
   `bAddToLoadout = true` (see [Modding.md](Modding.md)).
Blueprint subclasses work too for tuning (all numbers are `EditDefaultsOnly`).

### Adding a game mode
Subclass the closest mode (`AMFGameMode_TDM`, `AMFGameMode_Holdout`): override `ScoreKill`,
`CheckEndConditions`, `IsStartAllowed`, `OnTimeLimit`, `GetBotGoals`. Objectives from
converted maps are `AMFLegacyActor`s: `AMFLegacyActor::FindAll(World, "RageFlagBase")` gives
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
- `UMFImportCommandlet` (`-run=MFImport`): runs the steps below.
- `FMFImporter`: textures (AssetTools import + mobile settings), sounds, master materials
  (`/Game/MF/Materials/M_MF_*`, created in code), material instances per texture+mode,
  static meshes from `.mfmesh` (MeshDescription, lightmap UVs, collision), our own art
  (`ImportArt`: `Tools/artgen/out` -> `/Game/MF/...`), and `BuildMap()` (`MFLevelBuilder.cpp`)
  which spawns the `level.json` records into a new level, places ladder nav links and bakes the
  navmesh. Every converted asset goes under the root of its content pack (`FMFPacks`, from
  `work/out/packs.json`).

## `Tools/mfport` (Python)
See [Pipeline.md](Pipeline.md). MF-specific knowledge lives here (actor mapping, light
colour formula, poly flags); the C++ importer stays generic.
