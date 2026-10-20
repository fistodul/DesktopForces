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

Keys that run commands are a config table, like MF's User.ini: `[/Script/MobileForces.MFPlayerController]`
`+CommandBindings=(Key=F9,Command="SShot")` in `Config/DefaultGame.ini` (or your user Game.ini).
Built-in keys: WASD / arrows / `,` `.` move, Ctrl crouch (MF also had C; C opens orders here),
Delete / PageDown look up / down, Q / Backspace loadout, `;` throw, `[` `]` / wheel weapons.

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
| `MFTeleport X Y Z`, `MFHurt N`, `MFFire` | move your pawn, take damage, press fire once (scripted tests) |
| `MFTopView X Y Width [Z]` | orthographic top view (`MFTopView 0 0 0` = back) |
| `MFCam X Y Z Yaw Pitch` | fixed camera for screenshots |
| `MFLoadoutPick N`, `MFLoadout`, `MFSelect N`, `MFScores`, `MFShot Name` | for scripted runs: `-MFExec="@4:MFLoadoutPick 1\|@6:MFLoadout\|@20:MFShot test"` |

Not ported (no equivalent yet): vehicle (`TryEnterExit`, `KillCars`, `ExpireCars`), flag
(`TossFlag`), `FeignDeath`, `Taunt`, `Speech`, `BehindView` (third person comes with soldier models).
