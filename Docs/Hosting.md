# Hosting a server

Desktop Forces uses Unreal's own networking: one machine runs the match (the server), players
connect to its address (UDP, port 7777 unless `-port=` says otherwise; open it in the
firewall / router). Players join from the main menu (**Join Game**, `address[:port]`) or with
`open address:port` in the `~` console.

## Ways to run one

**From the game (listen server).** Main menu > Play > tick *Host for LAN / internet* > Start.
You play on it and are its admin. A packaged build can do the same from a command line:

```bash
./DesktopForces.sh "mf-warehouse?game=tdm?listen?Bots=6"
```

**Dedicated (no player), with the project and the engine installed.** The editor binary runs
a headless server; this works with the Launcher / binary engine:

```bash
<UE>/Engine/Binaries/Linux/UnrealEditor "$PWD/DesktopForces.uproject" \
  "/Game/Maps/BR-thebigisland?game=br?Bots=8" \
  -server -log -nosound -port=7777 \
  "-ini:Game:[/Script/DesktopForces.DFGameMode]:AdminPassword=change-me"
```

(Windows: `UnrealEditor.exe` / `UnrealEditor-Cmd.exe`.) Map paths: `/Game/Maps/<map>` for the
core maps, `/MobileForcesClassic/Maps/<map>` for classic ones (the server needs the converted
classic content too). A standalone server binary (`DesktopForcesServer`) needs an engine built
from source; it isn't set up yet.

## Match options (URL)

Add to the map: `map?option=value?option=value`.

| Option | Does |
|---|---|
| `game=<mode>` | the mode by its short name: `dm`, `tdm`, `holdout`, `ctf`, `sc` (Safecracker), `det` (Detonation), `trailer`, `cap` (Captains), `koh` (King of the Hill), `arena`, `br` (Battle Royale), `coop`, `deball`, `tug`, `zombie` (`GameModeClassAliases` in `Config/DefaultEngine.ini`; a class path works too) |
| `Bots=N`, `BotSkill=0..1` | bots added at the start |
| `TimeLimit=min`, `ScoreLimit=N`, `ScoreLimit=N` | limits (0 = the mode's default) |
| `Teams=N` | number of teams in team modes (MF: 2) |
| `RespawnWait=s` | seconds before a respawn |
| `Vehicles=0` | no vehicles; `Vehicles=Buggy` (any vehicle class: `Humvee`, `Jeep`, `Heli` ...) every road generator makes that one, `Vehicles=Random` one of the four stock cars each time; `Trucks=Jeep` / `Trucks=Heli` what the truck generators make; `APCs=ArmedAPC` / `APCs=ArmedAPCRocket` what the APC generators make; `TurboCars=0` no nitro; `AddVehicles=0` none of the server's added vehicles |
| `OPWeapons=1` | Gregaras' overpowered weapons: no clip ever runs dry, single-use items too (zombie survivors and the `InfiniteAmmo` cheat only get it for weapons with more than one use), shots 4x as fast (`OPFireRate`) |
| `Boomstick=1` | shotguns are the zombie mode's boomstick: 12 pellets × 15 per barrel, 22 spare clips, less kick (on by default in Zombie mode; `Boomstick=0` turns it off there) |
| `ImprovedRL=1` | Gian's improved rocket launcher instead of MF's: carry size 3, rockets 1800 uu/s, 140 damage |
| `NoWeapons=Grenades,RocketLauncher` | weapons loadouts don't hand out, by class name (their variants too: no rocket launchers of any kind) |
| `Gravity=x` | world gravity times x (`0.2`: FNG's LowGrav) |
| `Cheats=1` | cheats for everyone (else admins only) |
| `listen` | (listen servers) accept players |

## Server settings (Game.ini)

`Saved/Config/<platform>/Game.ini` on the server (or `-ini:Game:[Section]:Key=Value`):

```ini
[/Script/DesktopForces.DFGameMode]
AdminPassword=change-me      ; AdminLogin <password>; empty: nobody logs in remotely
bAllowCheats=False

; after a match: the next map of the list (MF MapList), else the same map again; maps this
; server hasn't converted are skipped; the server's own options (listen, Bots ...) carry over
+MapRotation=mf-warehouse?game=ctf
+MapRotation=mf-Sawmill?game=det?TimeLimit=10
; said to everyone every ServerMessageInterval seconds, ServerMessageGroup lines at a time
+ServerMessages=Welcome! Votes: callvote nextmap / restart / addbots N
ServerMessageInterval=600
ServerMessageGroup=1
; the same as the URL options above, for every match
;NoWeapons=Grenades
;AllVehicles=Random     ; ?Vehicles=Random
;Trucks=Heli
;APCs=ArmedAPC
;bOPWeapons=True
;bBoomstick=True

[/Script/DesktopForces.DFGameSettings]
; extra vehicles and swaps for this server only (maps already carry the community's ones)
+AddedVehicles=(Map="mf-warehouse",VehicleClass="heli.heli",Location=(X=0,Y=0,Z=200),Yaw=90)
+VehicleSwaps=(Map="mf-Carpark",From="Rage.Truck",To="jeep.jeep")
```

<details><summary>The community server's map list (MobileForces.ini <code>[Engine.MapList]</code>) as a rotation</summary>

```ini
[/Script/DesktopForces.DFGameMode]
+MapRotation=mf-HighwayRun?game=ctf
+MapRotation=mf-Sawmill?game=det
+MapRotation=mf-Toreford?game=tdm
+MapRotation=mf-Carpark?game=sc
+MapRotation=mf-Space_Station?game=ctf
+MapRotation=MF-Aquarius?game=holdout
+MapRotation=mf-dockyard?game=trailer
+MapRotation=mf-treasure_finders?game=det
+MapRotation=MF-Test_Facility?game=holdout
+MapRotation=mf-dockyard?game=sc
+MapRotation=mf-western?game=ctf
+MapRotation=mf-Rail_Quarry?game=cap
+MapRotation=mf-The_Bank?game=tdm
+MapRotation=DM-DrainageTrouble-KF?game=dm
+MapRotation=mf-warehouse?game=ctf
+MapRotation=mf-Pobojowisko_VerII?game=holdout
+MapRotation=DM-Office?game=dm
+MapRotation=mf-sawmill?game=cap
+MapRotation=mf-Carpark?game=holdout
+MapRotation=mf-Ghetto?game=sc
+MapRotation=mf-Polar?game=trailer
+MapRotation=dm-holidaytown?game=dm
+MapRotation=MF-DesertWarzVs2?game=det
+MapRotation=mf-BigBridge?game=holdout
+MapRotation=DM-KnifeWarehouse?game=dm
+MapRotation=mf-waterfront?game=ctf
+MapRotation=mf-City_Rush?game=cap
+MapRotation=mf-BattleBayNoonV1?game=sc
+MapRotation=DM-Asylum?game=dm
+MapRotation=mf-Polar?game=ctf
+MapRotation=mf-airport?game=det
+MapRotation=mf-Hydroworks?game=trailer
```
</details>

The FNG mutator pack's ideas live on as options: ServerAds = `ServerMessages`, EnableRotation =
`MapRotation`, LowGrav = `Gravity=0.2`, AllBuggys ... AllRandom = `Vehicles=<class>|Random`, NoGrenades / NoRocketLauncher /
NoTripBombs = `NoWeapons`; AutoDemoRec: UE records demos itself (`demorec <name>` ... `demostop` in
the console: `Saved/Demos/<name>.replay`; playing them back is untried).

Each mode's own options are in its class (`[/Script/DesktopForces.ZombieGame]`
`ZombieWeapons=2`, ...); [Gameplay.md](Gameplay.md) describes them.

## Running it

Log in with `AdminLogin <password>`, then (see [Commands.md](Commands.md#admin)):
`Kick <name>`, `SwitchMap <map?game=...>`, `AddBots N`, `Admin <any console command>`;
players vote with `CallVote travel <map> [mode]`, `CallVote nextmap`, `Vote yes`.

## Match limits and parachutes

Use `?ScoreLimit=N` for the mode's score target; 0 disables the target. The old separate
limit names are not accepted. `?Parachutes=0` disables parachutes server-wide; `=1` enables
them. The rule is replicated to clients. In Game.ini, the corresponding defaults are
`ScoreLimit` and `bParachutes` under `[/Script/DesktopForces.DFGameMode]` or a mode subclass.
