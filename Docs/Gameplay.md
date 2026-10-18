# Gameplay reference (original values)

Sources: decompiled UnrealScript of the retail game (`Engine`, `Rage`, `RageGame`,
`RageWeapons` packages), `System/MobileForces.ini`, `Manual.pdf`, and animation data read
from `RageWeapons.u`. Distances: MF units (uu); in code ×2.25 → cm.

## Player (Pawn / RPlayer / RagePlayer)
| | MF | UE5 |
|---|---|---|
| Collision cylinder | radius 17, half-height 39 | capsule 38.25 × 87.75 |
| Crouched half-height | 20 | 45 |
| Eye height above centre | 27 | 60.75 |
| Run speed (GroundSpeed) | 400 | 900 |
| Acceleration (AccelRate) | 2048 | 4608 |
| Air control | 0.35 | 0.35 |
| Jump (JumpZ) | 325 | 731.25 |
| Step height | 25 | 56.25 |
| Gravity (ZoneGravity) | −950 | −2137.5 (project physics) |
| Ground friction | 8, applied ×2 | GroundFriction 16; braking 8 × factor 2 |
| Walk / crouch speed | ×0.3 (UE1 native) | ×0.3 |
| Swim speed / ladder speed | 200 / 200 | 450 / 450 |
| Health | 100 | 100 |
| FOV | 100 | 100 |
| Fall damage | below −750 uu/s: 100 × ((Vz+750)/(−450))², dead at −1200 | same in cm/s |
| Carry | 6 blocks; speed −5%/block, min 50% | same |
| Headshot | hit Z − centre Z > 0.8 × half-height | same |
| Recoil | AddRecoil(RecoilPower×6); per frame pitch += r×20 rot units, yaw ±r×20, r halves | stepped at 30 Hz |

## Rules (RageDeathMatch / RageTeamGame)
- **Hardcore mode** (default on): damage from any instigator ×1.5.
- DM: frag limit 30, time limit 7 min. TDM: no frag limit, 12 min, team score per kill,
  friendly fire 0. CTF 4/20, Holdout 6/20, Detonation 4/10, Safe Cracker 1/9,
  Trailer 4/10, Captains 5/12 (frag-or-goal / minutes; to port).
- Respawn after max(1, RespawnWait) s; RespawnWait 0 for DM/TDM, 5 for CTF/Det/SC/Trailer.
- Spawn choice: neutral starts (TeamNumber ≥ 2) in non-team games, own team's starts in team
  games; random score, −10000 for the last spot used, players nearby lower the score.
- Kill +1, suicide −1; killing sprees every 5 kills; first blood message.
- Map end: 15 s (RestartWait) then restart.

## Loadout
- Crate trigger radius 129, height 26. DM crates only exist in non-team games, team crates
  and loadout zones only in team games.
- Items (LoadoutCrate.aWeaponsInStore): Pistol, Machine Gun, Shotgun, Sniper Rifle,
  Trip Bombs, Rocket Launcher, Grenades, Adrenaline, Armour, Heavy Machine Gun.
- Entering loadout refills ammo of everything carried. Loadout key away from loadout drops
  the held weapon. Loadout only while walking on the ground.

## Weapons
Timing = animation length: (frames − 1) / (seq rate × play rate) + tween.

| Weapon | Group | Carry | Clip × clips | Damage | Spread (Acc / zoomed) | Fire time | Reload | Notes |
|---|---|---|---|---|---|---|---|---|
| Knife | 1 | 0 | — | 30 slash (range 90) | — | ~0.55 s | — | alt: throw, 60 (head 250), returns after 6 s |
| Pistol | 2 | 1 | 8 × 6 | 26 (head 40) | 0.4 / 0.05 | 0.342 s | 1.55 s | alt: aim 1.5× |
| Machine gun | 3 | 2 | 48 × 5 | 20 | 0.7 / 0.1 | 0.117 s | 2.25 s | alt: aim 2× (single shots, 0.333 s) |
| Shotgun | 4 | 2 | 2 × 20 | 10 pellets × 12 per barrel | 0.8 / 1.0 | 0.472 s | 0.867 s | fire = left, alt = right, both within 0.1 s = both; range 5000 |
| Sniper rifle | 5 | 3 | 4 × 4 | 20 × power (1–10), head ×3 | 0.3 / 0 | 0.55 s | 1.383 s | power +2/s; scope 3×–30× (wheel); range 20000 |
| HMG | 6 | 4 | 900 × 1 | 45 | 0.6 / 1.2 | — | — | must deploy (alt); TODO |
| Rocket launcher | 7 | 4 | 1 × 11 | 135, radius 400 | — | 0.79 s | 3.38 s | rocket 1200 uu/s; alt: scope 4× + lock-on (TODO) |
| Grenades | 8 | 1 (max 3) | 3 | 160 r400 (timed 3 s) / 120 impact | — | 0.333 s | — | throw power 1.5–10 (+8/s), speed power × 150 uu/s, +210 up |
| Trip bombs | 9 | 1 (max 3) | 2 | — | — | — | — | laser 1024 uu; alt: proximity mine (TODO) |
| Adrenaline | 10 | 1 | 1 | full heal | — | 0.5 s | — | alt: inject team mate (range 90) |
| Armour | 11 | 1 | — | absorbs 75% | — | — | — | 100 charge; falling/drowning bypass |

Base trace: start at the eye, end = start + Acc×(rand−0.5)×1000·Y + Acc×(rand−0.5)×1000·Z
+ MaxTraceDistance (10000)·X.

## Vehicles (to port)
Humvee: 4 seats, armour 0.68, kill speed 8; types Humvee/Truck/Buggy/Armadillo (APC);
handling data in `Physics/handling.hnd2` + `.COL` collision files (format to reverse).
Bullets don't hurt vehicles (non-explosive damage = 0). 64 physical objects per level.
