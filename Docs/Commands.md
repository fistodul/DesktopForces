# Console commands

Like Mobile Forces: **Tab** opens the one-line console (MF `Type`), **`~`** (tilde / the key
left of 1) the full console with the log, `~` again closes either. Names follow Mobile Forces
where the original had the command; case doesn't matter.

## Everyone

| Command | Does |
|---|---|
| `Suicide` | kill yourself and respawn (stuck in geometry, fell somewhere unreachable) |
| `TryLoadout` | open / leave the loadout screen (Q, Backspace); away from loadout: drop the held weapon |
| `LoadoutNext` / `LoadoutPrev` / `LoadoutSelect` | move the loadout cursor / take or put back (S·W·wheel / Fire·E·Enter) |
| `SwitchWeapon N` | select group N (1-9, 10 = key 0, 11 = X) |
| `NextWeapon` / `PrevWeapon` | cycle weapons (wheel down / up, `]` / `[`); zoom the sniper scope out / in |
| `ThrowWeapon`, `Drop` | drop the held weapon (`;`) |
| `ShowScores` | toggle the scoreboard (F1 hold) |
| `Type` | the one-line console (Tab) |
| `SShot` | screenshot (`Saved/Screenshots/...`) |
| `GrowHUD` / `ShrinkHUD` | HUD size 0-4 (32 … 128 px item boxes), saved (also Options > Game) |
| `ChangeCrosshair` | next of the 24 MF crosshairs, saved (also Options > Game, with a preview) |
| `BehindView [0\|1]` | third-person camera (UE1's: 150 uu behind your view, pulled in at walls) / back in your eyes; no number: the other one |
| `FOV N` | field of view (UE) |
| `RestartLevel` | restart the map (single player / host) |
| `AddBots N`, `GoodBot N`, `BadBot N` | add bots of normal / better / worse skill (single player / host) |
| `ListBots` | bots and their skill |
| `CallVote travel <map> [mode]`, `CallVote nextmap / restart / addbots N / kickbots / shuffle / balance` | call a vote (Gregaras' vote mutator; you vote yes; over half the players saying yes passes it, 30 s) |
| `Vote yes`, `Vote no` | vote on the running vote |
| `Mutate callvote ...`, `Mutate vote y / n` | the same, as typed on the MF servers |
| `ShowCommandMenu` | team orders menu (V); number keys or a mouse click pick the order, then All / a team-mate (the view holds still while it's open) |
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

Keys that run commands are a config table, like MF's User.ini: `[/Script/DesktopForces.DFPlayerController]`
`+CommandBindings=(Key=F9,Command="SShot")` in `Config/DefaultGame.ini` (or your user Game.ini).
Built-in keys (MF DefUser.ini): WASD / arrows / `,` `.` move, Ctrl / C crouch (drivers: nitro),
Delete / PageDown look up / down, Q / Backspace loadout, `;` throw, `[` `]` / wheel weapons.
UE's development-build debug keys (F1 wireframe, F2-F5 view modes, PageUp/PageDown, `;` debug
camera) are switched off in `Config/DefaultInput.ini`; only Ctrl+Shift+`.` (recompile shaders)
and Ctrl+Shift+`,` (GPU profile) stay.

Without a map on the command line the game starts at the **main menu** (play: map, game type -
MF's own first, then the community's - bots, skill, time limit, LAN host; join an address;
options in tabs: **Video** resolution, display mode, graphics level, render scale, brightness,
field of view, bullet holes / scorch marks; **Audio** volume, music; **Controls** mouse, every
key (click it, press the new one; a key already in use swaps); **Game** name, crosshair, HUD
size; **Defaults** puts MF's options back, Apply keeps them). Options live in
`Saved/Config/<platform>/GameUserSettings.ini` (`UDFUserSettings`).

## Admin

Like MF's `bAdmin`: the listen server's host and single player are admins; others log in with
the server's password (`[/Script/DesktopForces.DFGameMode] AdminPassword=...` in the server's
Game.ini, or `-ini:Game:[/Script/DesktopForces.DFGameMode]:AdminPassword=...` on its command line).

| Command | Does |
|---|---|
| `AdminLogin Password` / `AdminLogout` | become / stop being admin |
| `Admin Command` | run a command on the server: any cheat, and for admins any console command (`Admin servertravel mf-warehouse?game=ctf`) |
| `Kick Name` | throw a player out (admins) |
| `SwitchMap URL` | change map, e.g. `SwitchMap mf-warehouse?game=/Script/DesktopForces.CaptureTheFlag` (admins) |
| `RunAs Name Command` / `RunAsLooking Command` | run a command as that player / the one you look at (ZombieGame) |
| `SetTo Name Property Value` / `GetTo Name Property` | set / read a property of a player's soldier (then player state, controller), e.g. `SetTo Bob MaxHealth 300` |
| `SetToSelf` / `GetToSelf`, `SetToLooking` / `GetToLooking` | the same on yourself / what you look at |
| `TeleportTo Name` | next to a player (ZombieGame's `Teleport`; UE's `Teleport` goes where you aim) |
| `BecomeHuman` / `BecomeSurvivor` / `BecomeZombie` / `BecomeNemesis` | Zombie mode: that side's stats on you (not a team change) |

## Cheats

For admins, everyone on a server started with `?Cheats=1` (`bAllowCheats`), and single player.
Typed on a client they run on the server, which checks you may (views like `DFCam` stay on
your machine and need cheats on too). The admin commands above work the same way.

| Command | Does |
|---|---|
| `God` | no damage (UE) |
| `TeamGod` | toggle no damage for your whole team |
| `Fly` / `Ghost` / `Walk` | fly, fly through walls, back to walking (UE) |
| `SetTeam N` | join team N (0 blue, 1 red, …) and respawn there |
| `KillEnemy` | kill every enemy (everyone else in deathmatch) |
| `KillPawns` | kill every bot |
| `KillAll Class` | kill / remove every actor of a class, e.g. `KillAll DFCharacter` |
| `AllAmmo` | full ammo |
| `GiveWeapon Name` | give yourself a weapon, e.g. `GiveWeapon RocketLauncher`, `GiveWeapon Molotov` |
| `SetWeapons Name` | give every bot that weapon |
| `SetTime S` | seconds left in the match |
| `SetSpeed X` | walk speed multiplier |
| `SetJumpZ V` | jump velocity, cm/s (default 731 = MF 325) |
| `Slomo X` | game speed (UE) |
| `Summon Class` | spawn an actor in front of you by its class name (UE), e.g. `summon Buggy`, `summon WoodenBox` |
| `Teleport` | to where you aim (UE) |
| `ViewMe` / `ViewSelf` | camera back to yourself |

## Developer / test

| Command | Does |
|---|---|
| `DFNavTest` | log navmesh reachability from you to every spawn, objective and ladder |
| `DFNavPath X Y Z` | log the navmesh route from you to a point (points, links) |
| `DFTraceMat` | log what the view looks at: meshes, materials, compile errors, lightmap coefficients |
| `DFTeleport X Y Z`, `DFHurt N`, `DFFire [Alt] [Seconds]` | move your pawn (a driven vehicle takes its trailer along), take damage, hold fire (scripted tests; default 0.1 s; `DFFire 1` alt fire) |
| `DFAimAt X Y Z`, `DFAimAtNearest Class` | turn to a point; to the nearest actor of a class (`DFAimAtNearest WoodenBox`) |
| `DFOrder N` | give team order N to everyone (0 Defend, 1 Hold, 2 Attack, 3 Cover me, 4 Freelance), as the V menu does |
| `DFHurtEnemies N` | N damage from you to every enemy (mode rules apply: Tug converts, Totem saves) |
| `DFTopView X Y Width [Z]` | orthographic top view (`DFTopView 0 0 0` = back) |
| `DFCam X Y Z Yaw Pitch` | fixed camera for screenshots |
| `DFLoadoutPick N`, `DFLoadout`, `DFSelect N`, `DFScores`, `DFShot Name` | for scripted runs: `-DFExec="@4:DFLoadoutPick 1\|@6:DFLoadout\|@20:DFShot test"` |
| `DFWalk Yaw Seconds [Fwd] [Right] [Up]` | hold movement input (in a vehicle throttle / steering; Up 1 Jump, -1 Crouch), logging position + movement mode every 0.25 s (stairs, ladder, vehicle tests) |
| `DFMenuPage N`, `DFMenuStart`, `DFShot Name` | main menu: show page N (0 main, 1 play, 2 join, 3 options, 4 credits; 10-13 the options tabs) / press Start / screenshot with the menu |
| `DFEnterSeat N` | get into seat N (0 driver, 1.. passengers) of the nearest vehicle |
| `DFBlowUp` | blow up the nearest vehicle (explosive damage: debris, wreck) |
| `DFVehicles` | log every vehicle: position, mass, gravity, physics awake, height above ground, wheel compression |
| `DFDecal 0\|1 [Size]` | a bullet hole / scorch mark where you look |
| `DFNavRelevant` | log moving things that can dirty the navmesh ("NAVMESH NEEDS TO BE REBUILT" hunts) |
| `DFApplyUserSettings` | re-read the player's options (field of view, keys) |

Command line for automated runs: `-DFExec="…"` (commands, `@seconds:` delays), `-DFQuitAfter=N`,
`-DFAutoShot=N -DFShotName=x [-DFShotUI]` (`-DFShotUI` includes Slate menus), `-abslog=<file>`
to keep a run's log apart from a parallel editor/bake (they share `Saved/Logs/DesktopForces.log`).

Not ported (no equivalent yet): `KillCars`, `ExpireCars`,
`FeignDeath`, `Taunt`, `Speech`.
