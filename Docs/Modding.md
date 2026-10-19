# Modding

Mods are **Unreal plugins** under `Plugins/Mods/<ModName>/`. A plugin can add C++ code,
Blueprints, assets and config, and players turn it on or off without touching the game.
Two working examples ship with the repo:

| Plugin | Original (MF-mods) | Shows |
|---|---|---|
| `MFModMolotov` | `Molotov` | a new weapon (subclass of Grenades), its projectiles, a burning area actor, a damage type, its own icon art |
| `MFModKOH` | `KOH` | a new game mode (subclass of Holdout): several capture points chosen from the map's objectives, per-second scoring |

## Is the JSON the mod format?

No. `level.json` and `.mfmesh` are **pipeline intermediates**: they exist only so the
Python converter can hand original MF data to the C++ importer, and they're thrown away
after import. Mods never see them.

What mods use instead is standard UE, which tools, docs and other UE modders already know:

| Need | Use |
|---|---|
| New weapon / game mode / actor | C++ or Blueprint subclass in a plugin |
| Tuning numbers | `EditDefaultsOnly` properties (Blueprint child or plugin config) |
| Lists (teams, loadout, bots) | `UMFGameSettings` (Project Settings > Game > Mobile Forces, saved in `Config/DefaultGame.ini`) |
| Tables of data (many rows) | DataTables (import/export **CSV or JSON** natively) |
| Art and sound | plugin `Content/`, or `SourceArt/` converted by the pipeline (below) |

So JSON is still there if you like it — as DataTable import — but the source of truth is
UE assets and config, not files we invented.

## Anatomy of a mod

```
Plugins/Mods/MFModMolotov/
├── MFModMolotov.uplugin          name, description, credits, "EnabledByDefault"
├── Config/DefaultGame.ini        optional settings for the mod's own classes
├── SourceArt/Icons/MolotovIcon.bmp   art the mod author owns (committed)
├── Content/Generated/...         imported from SourceArt (git-ignored, rebuilt by the pipeline)
└── Source/MFModMolotov/
    ├── MFModMolotov.Build.cs     depends on "MobileForces"
    ├── Public/MFMolotov.h
    └── Private/MFMolotov.cpp, MFModMolotovModule.cpp
```

The module depends on the `MobileForces` module and subclasses its classes. Keep class
chains shallow: subclass the closest stock class once, don't stack mod-on-mod-on-mod.

### A weapon (MFModMolotov)

```cpp
AMFWeapon_Molotov::AMFWeapon_Molotov()
{
	DisplayName = LOCTEXT("Molotov", "Molotov Cocktail");
	MaxClipAmmo = 2;
	bAddToLoadout = true;   // appears in every loadout crate while the plugin is enabled
	ProjectileClass = AMFProjectile_Molotov::StaticClass();
	HUDIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/MFModMolotov/Generated/Icons/MolotovIcon.MolotovIcon")));
	HUDIconRect = FIntRect(0, 0, 64, 64);
}
```

`bAddToLoadout` registers the weapon by itself. Plugin config files are *not* merged
into the game's settings, so editing the stock loadout list from a plugin's ini doesn't
work. To choose the order, list it in the project's `Config/DefaultGame.ini`:
`+LoadoutItems=/Script/MFModMolotov.MFWeapon_Molotov`.

### A game mode (MFModKOH)

Subclass the closest mode and override only what differs. KOH changes how points are
picked (`SpawnPoints`), how they score (`UpdatePoints`) and the capture bonus
(`PointCaptured`); spawning, teams, bots, HUD and match flow are inherited. Play it with

```
<map>?game=/Script/MFModKOH.MFGameMode_KOH?Bots=8
```

Rules are `Config` properties, so servers tune them in the plugin's `Config/DefaultGame.ini`
or per match with URL options (`?GoalTeamScore=500`).

Bots get objectives from `AMFGameMode::GetBotGoals()` (best first); a mode that returns
nothing leaves them roaming and fighting.

### Art

Put the mod's own files in `SourceArt/` (`.bmp/.png/.tga` images, `.wav` sounds). Then

```bash
python3 Tools/mfport/mfport.py mods                    # convert (8-bit BMP: palette 0 = transparent)
python3 Tools/mfport/mfport.py import --steps plugins  # import into /<Plugin>/Generated/
```

Only commit art you have the rights to; the MF-mods sources are MIT.

## Checklist for porting an MF-mods package

1. Read the `.uc` classes; find the stock class each one extends.
2. Create the plugin (copy `MFModMolotov`, rename), depend on `MobileForces`.
3. One C++ class per mod class that adds behaviour. Constants go in constructors with a
   comment giving the original value (`// MF 300 uu`). Use UE cm in code.
4. Things MF did with subclass chains (player classes per mod, e.g. zombies) become
   components or settings on the stock classes instead.
5. Prefer engine features over copies of MF code: timers not `Tick`, `ULocalMessage` for
   announcements, `UDeveloperSettings` for lists, navmesh for bots.
6. Test: `-MFExec="@3:MFLoadoutPick 10|@20:MFShot test" -MFQuitAfter=25` runs a scripted
   match and takes screenshots (see `UMFGameInstance`).
