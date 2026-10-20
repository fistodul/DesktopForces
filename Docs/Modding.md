# Modding

Desktop Forces is a normal Unreal Engine 5.8 project, so modding means standard UE: C++ or
Blueprint subclasses, assets, config. This repository *is* the SDK: open
`DesktopForces.uproject` in the free UE editor and you have the map editor, the code and every
class the game uses.

## The MF-mods are part of the game

The community's MF-mods were ported once as plugins; they are now ordinary code in
`Source/DesktopForces/Mods/<Name>/` (2026-10-20: they always ship, so separate modules only
added build steps). They are still the best examples of how to extend the game:

| Folder | Original (MF-mods, fistodul) | Shows |
|---|---|---|
| `Mods/Molotov` | `Molotov` | a new weapon (subclass of Grenades), its projectiles, a burning area actor, a damage type, its own icon art |
| `Mods/KOH` | `KOH` | a new game mode (subclass of Holdout): several capture points chosen from the map's objectives, per-second scoring |
| `Mods/Totem` | `Totem` | a carried item that cheats death (`AbsorbDamage` + `GetAbsorbPriority`), fire / alt fire / reload actions, a heal-over-time **component** with a damage filter (`IMFDamageFilter`) |
| `Mods/TripLasers` | `TripLasers` | a trip bomb variant through the trip bomb hooks (`BombClass`, `CanPlace`, `Arm`/`CheckBeam`, mine `ShouldTrigger`/`Trigger`) |
| `Mods/SeekerMines` | `SeekerMines` | a thrown drone: an `APawn` with `UFloatingPawnMovement` + `AAIController` navmesh moves, hunting / escorting / roaming on a 5 Hz timer |
| `Mods/SentryGun` | `SentryGun` | a deployable turret on timers (no Tick), the HMG model reused |
| `Mods/Tug` | `TugGame` | a round-based team mode: `PreventDeath` (kills convert instead), `RestartRound`, original teams per round |
| `Mods/Deagle`, `Mods/GrenadeLauncher` | `deagle`, `grenLaunch` | stock weapon subclasses; models from the mod packages when converted, stock fallbacks |
| `Mods/PredatorMissile` | `PredatorMissile` | a player-guided missile actor, view-target switching, a weapon HUD overlay (`DrawHUD`) |
| `Mods/ArmedAPC` | `ArmedAPC` | a vehicle subclass with a gunner seat (`PassengerEntered` / `Left`), a turret, a hidden seat "weapon" |
| `Mods/Coop` | `CoopMode` (Animeman for Akvari) | humans vs bots on fixed teams (`ChooseTeam`), loadout limits (`AMFGameState::LoadoutAccess`, `LoadoutDenied`) |
| `Mods/Zombie` | `ZombieGame` (Animeman) | an asymmetric mode on top of Tug: per-team stats (`AMFCharacter::SetBodyScale`, `MaxHealth`, `MaxCarry`), item variants (`GetWeaponClassFor`), per-team loadout (`TeamLoadoutAccess`), team names (`TeamNameOverrides`), spawn rules (`IsStartAllowed`), a damage filter component |

Their art (icons) is in `SourceArt/Mods/Icons/`, imported to `/Game/MF/Mods/Icons/` by
`Tools/artgen` + `mfport.py import --steps art`.

### Hooks in the base game

| Hook | For |
|---|---|
| `AMFWeapon::AbsorbDamage` / `GetAbsorbPriority` | armour-like items, run highest priority first (armour 10, Totem -1) |
| `IMFDamageFilter` (any component on a character) | buffs, shields, grace periods: see damage before armour |
| `AMFGameMode::PreventDeath` | keep a dying player alive (convert, revive, last stand) |
| `AMFGameMode::RestartRound` | round modes: clock restart, clear planted / dropped items, respawn everyone |
| `AMFGameMode::GetBotGoals` / `GetBotShootTarget` | where bots go / what they shoot in your mode |
| `AMFGameMode::VehicleEntered`, `GetVehicleExclusions` | vehicle modes (Trailer) |
| `AMFWeapon_TripBombs`: `BombClass`, `bNeedLaserTarget`, `CanPlace`, `GetThrowSpeed` | placed / thrown devices |
| `AMFWeapon::bAddToLoadout` | the item appears in every loadout crate |
| `AMFGameMode::ChooseTeam`, `GetWeaponClassFor`, `LoadoutDenied` | who plays where, a mode's own item variants, the "no loadout" answer |
| `AMFGameState::LoadoutAccess`, `TeamLoadoutAccess`, `TeamNameOverrides` | where loadouts open (per team), mode team names |
| `AMFCharacter::SetBodyScale` | replicated speed / jump / accel / step / fall multipliers (zombies, buffs) |
| `AMFWeapon::IsMeleeWeapon` | bots run at the target instead of shooting from range |

## Adding things

| Need | Use |
|---|---|
| New weapon / game mode / actor | C++ or Blueprint subclass (closest stock class, once: keep chains shallow) |
| Tuning numbers | `EditDefaultsOnly` properties (Blueprint child or config) |
| Lists (teams, loadout, bots, menu pictures) | `UMFGameSettings` (Project Settings > Game, saved in `Config/DefaultGame.ini`) |
| Tables of data (many rows) | DataTables (import/export **CSV or JSON** natively) |
| Art and sound | `SourceArt/` (converted by `Tools/artgen`) or assets made in the editor |
| Maps | the UE editor, with the game's actor classes (below) |

`level.json` and `.mfmesh` are **pipeline intermediates** for converting the original game's
maps; they are thrown away after import and mods never see them.

### A weapon (Molotov)

```cpp
AMFWeapon_Molotov::AMFWeapon_Molotov()
{
	DisplayName = LOCTEXT("Molotov", "Molotov Cocktail");
	MaxClipAmmo = 2;
	bAddToLoadout = true;   // appears in every loadout crate
	ProjectileClass = AMFProjectile_Molotov::StaticClass();
	HUDIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/MF/Mods/Icons/MolotovIcon.MolotovIcon")));
	HUDIconRect = FIntRect(0, 0, 64, 64);
}
```

`bAddToLoadout` registers it by itself; to choose its place, list it in `Config/DefaultGame.ini`
(`+LoadoutItems=/Script/DesktopForces.MFWeapon_Molotov`).

### A game mode (KOH)

Subclass the closest mode and override only what differs. KOH changes how points are picked
(`SpawnPoints`), how they score (`UpdatePoints`) and the capture bonus (`PointCaptured`);
spawning, teams, bots, HUD and match flow are inherited. Play it with
`<map>?game=/Script/DesktopForces.MFGameMode_KOH?Bots=8`. Rules are `Config` properties, so
servers tune them in `DefaultGame.ini` or per match with URL options (`?GoalTeamScore=500`).
Bots get objectives from `AMFGameMode::GetBotGoals()` (best first).

### A map

Make it in the UE editor like any UE level (BSP brushes or the modeling tools, static meshes,
lights; bake with Build > Lighting). Place the game's actors: `MFPlayerStart` (team number),
`MFLoadoutCrate` / `MFLoadoutZone`, `MFLadderVolume`, the mode objectives (`MFHoldoutPoint`,
`MFSafe`, `MFFlagBase`, `MFDetonationLock`; trailer bays and vehicle generators are still the
converted MF actors), `MFMapInfo` (title, author, music, minimap). Save it under `/Game/Maps/` and it shows up in the menu. Keep to the
[performance rules](Performance.md): static lighting, textures up to 512 px.

## Plugins: what they cost and when they load

- A **code plugin** compiles to its own module. In a packaged game all modules end up in the
  game binary (monolithic build), so at runtime a plugin costs nothing over code in the game
  module; in the editor it is one more library and a bit more build time.
- Plugins are found **when the game or editor starts** (`Plugins/` of the project or engine,
  enabled in the `.uproject` or by default) - not while playing.
- A **content-only plugin** (no code) is just a folder of assets with its own mount point;
  that's what *Mobile Forces Classic* is: the original game's content, separable from ours.

## Content after release

A packaged UE game can take new content without rebuilding the game, as long as it was cooked
with the same engine version:

- **Maps, Blueprints, assets**: cook them in this project (editor or `RunUAT BuildCookRun` with
  the DLC options) into a `.pak` and drop it into the game's `Content/Paks/` folder; UE mounts
  every pak there at start. Blueprint mods can add weapons and modes this way, no compiler
  needed. (Planned: a `Mods/` folder the game scans and a menu list of mod maps -
  [Roadmap](Roadmap.md).)
- **C++ code**: needs a build of the game itself (open source: fork, add code, build) - the same
  as UnrealScript `.u` packages needed UCC in MF, but compiled native code instead of bytecode.

Only commit art you have the rights to. The MF-mods code is by fistodul (MPL-2.0 now, MIT
before).

## Checklist for porting an MF-mods package

1. Read the `.uc` classes; find the stock class each one extends.
2. Add `Source/DesktopForces/Mods/<Name>/` with one C++ class per mod class that adds behaviour.
   Constants go in constructors with a comment giving the original value (`// MF 300 uu`).
   Use UE cm in code.
3. Things MF did with subclass chains (player classes per mod, e.g. zombies) become components
   or settings on the stock classes instead.
4. Prefer engine features over copies of MF code: timers not `Tick`, `UDeveloperSettings` for
   lists, navmesh for bots.
5. Icons and sounds the mod author owns go in `SourceArt/Mods/`.
6. Test: `-MFExec="@3:MFLoadoutPick 10|@20:MFShot test" -MFQuitAfter=25` runs a scripted match
   and takes screenshots (see `UMFGameInstance`, [Commands](Commands.md)).
