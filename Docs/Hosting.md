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
| `TimeLimit=min`, `FragLimit=N`, `GoalTeamScore=N` | limits (0 = the mode's default) |
| `Teams=N` | number of teams in team modes (MF: 2) |
| `RespawnWait=s` | seconds before a respawn |
| `Vehicles=0` | no vehicles; `TurboCars=0` no nitro; `Jeeps=1` / `Helis=1` jeeps or helis at the truck generators; `ArmedAPC=1\|2` armed APCs; `AddVehicles=0` none of the server's added vehicles |
| `ChangeDamage=0` | MF's own damage rules instead of the community servers' (mutPack) |
| `Cheats=1` | cheats for everyone (else admins only) |
| `listen` | (listen servers) accept players |

## Server settings (Game.ini)

`Saved/Config/<platform>/Game.ini` on the server (or `-ini:Game:[Section]:Key=Value`):

```ini
[/Script/DesktopForces.DFGameMode]
AdminPassword=change-me      ; AdminLogin <password>; empty: nobody logs in remotely
bAllowCheats=False

[/Script/DesktopForces.DFGameSettings]
; extra vehicles and swaps for this server only (maps already carry the community's ones)
+AddedVehicles=(Map="mf-warehouse",VehicleClass="heli.heli",Location=(X=0,Y=0,Z=200),Yaw=90)
+VehicleSwaps=(Map="mf-Carpark",From="Rage.Truck",To="jeep.jeep")
```

Each mode's own options are in its class (`[/Script/DesktopForces.ZombieGame]`
`ZombieWeapons=2`, ...); [Gameplay.md](Gameplay.md) describes them.

## Running it

Log in with `AdminLogin <password>`, then (see [Commands.md](Commands.md#admin)):
`Kick <name>`, `SwitchMap <map?game=...>`, `AddBots N`, `Admin <any console command>`;
players vote with `CallVote travel <map> [mode]`, `CallVote nextmap`, `Vote yes`.
