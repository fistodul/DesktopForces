# Console commands

Open the console with **`~`** (tilde / the key left of 1). Names follow Mobile Forces where
the original had the command; case doesn't matter.

## Everyone

| Command | Does |
|---|---|
| `Suicide` | kill yourself and respawn (stuck in geometry, fell somewhere unreachable) |
| `TryLoadout` | open / leave the loadout screen (Q, Backspace); away from loadout: drop the held weapon |
| `LoadoutNext` / `LoadoutPrev` / `LoadoutSelect` | move the loadout cursor / take or put back (S·W·wheel / Fire·E·Enter) |
| `SwitchWeapon N` | select group N (1-9, 10 = key 0, 11 = X) |
| `NextWeapon` / `PrevWeapon` | cycle weapons (wheel down / up, `]` / `[`); zoom the sniper scope out / in |
| `ThrowWeapon`, `Drop` | drop the held weapon (`;`) |
| `ShowScores` | toggle the scoreboard (F1 / Tab hold) |
| `SShot` | screenshot (`Saved/Screenshots/...`) |
| `GrowHUD` / `ShrinkHUD` | HUD size 0-4 (32 … 128 px item boxes), saved |
| `ChangeCrosshair` | next of the 24 MF crosshairs, saved |
| `FOV N` | field of view (UE) |
| `RestartLevel` | restart the map (single player / host) |
| `AddBots N`, `GoodBot N`, `BadBot N` | add bots of normal / better / worse skill (single player / host) |
| `ListBots` | bots and their skill |
| `ShowCommandMenu` | team orders menu (V, C); number keys pick the order, then All / a team-mate |
| `Talk` / `TeamTalk` | type a chat line to everyone / your team (T / Y) |
| `Say Text` / `TeamSay Text` | send a chat line |
| `ViewTeam` | watch the next team-mate (F5); `ViewPlayerNum N`: NumPad1 = yourself, NumPad2.. = team-mates |
| `CenterView` | level the view (End) |
| `Reload` | reload (R); Totem: heal yourself over time |
| `TossFlag` | drop the flag / gold / detonation key you carry (G) |
| `TryEnterExit` | get into the nearest vehicle / out of this one (E; Enter in the loadout picks) |
| `ShowMenu` | in-game menu: resume, new game, join, options, leave, quit (Esc) |
| `Brightness [X]` | MF brightness: step 0.5 → 2, or set 0.25 - 3 (scales the map's ambient light; saved) |
| `RenderScale X` | 3D resolution 0.25 - 2 × the window (HUD stays sharp; saved, also in Options) |

Keys that run commands are a config table, like MF's User.ini: `[/Script/DesktopForces.MFPlayerController]`
`+CommandBindings=(Key=F9,Command="SShot")` in `Config/DefaultGame.ini` (or your user Game.ini).
Built-in keys: WASD / arrows / `,` `.` move, Ctrl crouch (MF also had C; C opens orders here),
Delete / PageDown look up / down, Q / Backspace loadout, `;` throw, `[` `]` / wheel weapons.
UE's development-build debug keys (F1 wireframe, F2-F5 view modes, PageUp/PageDown, `;` debug
camera) are switched off in `Config/DefaultInput.ini`; only Ctrl+Shift+`.` (recompile shaders)
and Ctrl+Shift+`,` (GPU profile) stay.

Without a map on the command line the game starts at the **main menu** (play: map, game type,
bots, skill, time limit, LAN host; join an address; options: name, resolution, display mode,
graphics level, brightness, mouse, field of view). Options live in
`Saved/Config/<platform>/GameUserSettings.ini` (`UMFUserSettings`).

## Cheats

Single player only, like Mobile Forces (or after `EnableCheats` on a listen server).

| Command | Does |
|---|---|
| `God` | no damage (UE) |
| `TeamGod` | toggle no damage for your whole team |
| `Fly` / `Ghost` / `Walk` | fly, fly through walls, back to walking (UE) |
| `SetTeam N` | join team N (0 blue, 1 red, …) and respawn there |
| `KillEnemy` | kill every enemy (everyone else in deathmatch) |
| `KillPawns` | kill every bot |
| `KillAll Class` | kill / remove every actor of a class, e.g. `KillAll MFCharacter` |
| `AllAmmo` | full ammo |
| `GiveWeapon Name` | give yourself a weapon, e.g. `GiveWeapon RocketLauncher`, `GiveWeapon Molotov` |
| `SetWeapons Name` | give every bot that weapon |
| `SetTime S` | seconds left in the match |
| `SetSpeed X` | walk speed multiplier |
| `SetJumpZ V` | jump velocity, cm/s (default 731 = MF 325) |
| `Slomo X` | game speed (UE) |
| `Summon Class` | spawn an actor in front of you (UE) |
| `Teleport` | to where you aim (UE) |
| `ViewMe` / `ViewSelf` | camera back to yourself |

## Developer / test

| Command | Does |
|---|---|
| `MFNavTest` | log navmesh reachability from you to every spawn, objective and ladder |
| `MFNavPath X Y Z` | log the navmesh route from you to a point (points, links) |
| `MFTraceMat` | log what the view looks at: meshes, materials, compile errors, lightmap coefficients |
| `MFTeleport X Y Z`, `MFHurt N`, `MFFire` | move your pawn (a driven vehicle takes its trailer along), take damage, press fire once (scripted tests) |
| `MFHurtEnemies N` | N damage from you to every enemy (mode rules apply: Tug converts, Totem saves) |
| `MFTopView X Y Width [Z]` | orthographic top view (`MFTopView 0 0 0` = back) |
| `MFCam X Y Z Yaw Pitch` | fixed camera for screenshots |
| `MFLoadoutPick N`, `MFLoadout`, `MFSelect N`, `MFScores`, `MFShot Name` | for scripted runs: `-MFExec="@4:MFLoadoutPick 1\|@6:MFLoadout\|@20:MFShot test"` |
| `MFWalk Yaw Seconds [Fwd] [Right]` | hold movement input, logging position + movement mode every 0.25 s (stairs / ladder tests) |
| `MFMenuPage N`, `MFMenuStart` | main menu: show page N (0 main, 1 play, 2 join, 3 options, 4 credits) / press Start |
| `MFVehicles` | log every vehicle: position, mass, gravity, physics awake, height above ground, wheel compression |
| `MFApplyUserSettings` | re-read the player's options (field of view) |

Command line for automated runs: `-MFExec="…"` (commands, `@seconds:` delays), `-MFQuitAfter=N`,
`-MFAutoShot=N -MFShotName=x [-MFShotUI]` (`-MFShotUI` includes Slate menus), `-abslog=<file>`
to keep a run's log apart from a parallel editor/bake (they share `Saved/Logs/DesktopForces.log`).

Not ported (no equivalent yet): `KillCars`, `ExpireCars`,
`FeignDeath`, `Taunt`, `Speech`, `BehindView` (third person comes with soldier models).
