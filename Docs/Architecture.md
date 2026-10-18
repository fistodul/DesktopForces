# Architecture

Two C++ modules in one UE project, plus the Python pipeline.

## `MobileForces` (runtime game module)

| Folder | Class | Original MF class(es) | Role |
|---|---|---|---|
| `Core/` | `MFTypes.h` | `Pawn.Team`, damage type names | `EMFTeam` (0 blue, 1 red, 255 none), `UMFDamageType` subclasses (`Bullet` = 'tracedshot', `Shotgun` = 'scattershot', `Knife` = 'ArmourPiercing', `Headshot` = 'Decapitated', `Explosion`, `Fell`, `Drowned`, `Crushed`, `Suicide`) |
| `Core/` | `UMFGameInstance` | — | game-wide state (menus, settings later) |
| `Player/` | `AMFCharacter` | `Pawn` > `PlayerPawn` > `RPlayer` > `RagePlayer` | health, armour via inventory, carry blocks, inventory & weapon switching, fall damage, recoil, loadout visibility |
| `Player/` | `UMFCharacterMovement` | UE1 `PHYS_Walking` / `PHYS_Ladder` | MF movement constants, carry/aim/walk speed scaling, ladder custom mode, walk flag in saved moves |
| `Player/` | `AMFPlayerController` | `RagePlayer` input + loadout states | Enhanced Input built in code (original key map), loadout menu flow, weapon cycling, scoreboard |
| `Weapons/` | `AMFWeapon` | `Weapon` > `RageWeapon` | ammo (clip + spare clips), state machine (equip/idle/fire/reload/zoom/put down), MF `Finish()` logic, hitscan `TraceFire`, projectiles, replication |
| `Weapons/` | `AMFWeapon_*` (`MFWeapons.h`) | `RageKnife`, `Pistol`, `MachineGun`, `shotgun`, `RageRifle`, `RocketLauncher`, `Grenades`, `AdrenalineShot`, `RageArmour` | per-weapon numbers and special behaviour (twin barrels, charge shots, throw power, injecting, armour absorption) |
| `Weapons/` | `AMFProjectile_*` | `Rocket`, `Grenade1`, `Grenade2`, `Knife_Thrown` | `UProjectileMovementComponent`, MF HurtRadius (linear falloff) |
| `Loadout/` | `AMFLoadoutSource`, `AMFLoadoutCrate`, `AMFLoadoutZone` | `LoadoutCrate*`, `LoadoutZone` | where players may open loadout; item list; team / game-type relevance |
| `Game/` | `AMFGameMode` (+`_DM`, `_TDM`) | `RageGameInfo` > `RageDeathMatch` > `RageTeamGame` | rules, spawning (`FindPlayerStart`), damage modifiers, scoring, match end, respawn |
| `Game/` | `AMFGameState`, `AMFPlayerState`, `AMFPlayerStart` | `RageGameRepInfo`, `PlayerReplicationInfo`, `PlayerStart` | replicated rules/clock/scores; team/frags/deaths; team spawn points |
| `UI/` | `AMFHUD` | `RageHUD` | Canvas HUD: crosshair/scope, health/armour/ammo, weapon strip, clock, kill feed, loadout menu, scoreboard |
| `World/` | `AMFMapInfo`, `AMFLegacyActor` | `LevelInfo`, everything not ported yet | map title/music; preserved original actors (class, team, raw properties) for future game modes |

### Networking
Server-authoritative, UE replication from day one (listen and dedicated servers).
- Movement: `UCharacterMovementComponent` prediction; the walk flag rides in compressed flags.
- Weapons: client input → `Server*` RPCs; server runs the weapon state machine and applies
  damage; the owning client runs the same state machine for instant feedback; other
  clients play effects on `BurstCounter` changes. Ammo replicates to the owner only.
- Loadout: client only *displays* the menu (`AMFHUD`), picks go through `ServerLoadoutPick`.

### Adding a weapon
1. Subclass `AMFWeapon` (or an existing weapon) in `MFWeapons.h/.cpp`; set the MF numbers
   in the constructor with a comment naming the original value.
2. Override `FireShot`/`TryFire`/`StartFire` only for behaviour that differs.
3. Add it to `UMFLoadoutLibrary::DefaultItems()` (or a crate's `Items`).
Blueprint subclasses work too for tuning (all numbers are `EditDefaultsOnly`).

### Adding a game mode
Subclass `AMFGameMode` (see `AMFGameMode_TDM`): override `ScoreKill`,
`CheckEndConditions`, `IsStartAllowed`, `OnTimeLimit`. Objectives from converted maps are
`AMFLegacyActor`s: `AMFLegacyActor::FindAll(World, "RageFlagBase")` gives the flag bases
with their `Team` and original properties — replace them with real actors in `BeginPlay`.

## `MobileForcesEditor` (editor module)
- `UMFImportCommandlet` (`-run=MFImport`): runs the steps below.
- `FMFImporter`: textures (AssetTools import + mobile settings), sounds, master materials
  (`/Game/MF/Materials/M_MF_*`, created in code), material instances per texture+mode,
  static meshes from `.mfmesh` (MeshDescription, lightmap UVs, collision), and
  `BuildMap()` (`MFLevelBuilder.cpp`) which spawns the `level.json` records into a new level.

## `Tools/mfport` (Python)
See [Pipeline.md](Pipeline.md). MF-specific knowledge lives here (actor mapping, light
colour formula, poly flags); the C++ importer stays generic.
