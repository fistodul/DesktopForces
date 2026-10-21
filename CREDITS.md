# Credits

Keep this in step with the in-game credits (`MakeCredits` in `Source/DesktopForces/UI/MFMenu.cpp`).

## Desktop Forces

A remake and spiritual successor of Mobile Forces (Rage Software, 2002) by its community,
[mf.nofisto.com](https://mf.nofisto.com).

| Role | Who |
|---|---|
| Project lead, code | Filip Stojanovic and the Desktop Forces contributors (git history) |
| Mods (MF-mods, now part of the game) | fistodul: Totem of Undying, Trip Lasers, Seeker Mines, Sentry Gun, Tug of War, King of the Hill, Molotov |
| Coop, Zombie modes | Animeman (for AKVARI) |
| Grenade launcher | Gian, Peto, "Secret Dude" |
| MoBall, TurboCars, mutPack (changeDamage, vehicle swaps) | the MF community; mutPack edited by Gregaras |
| Battle Royale, Arena | Edgaras "Gregaras" Ziaugra (design, models: item crate, metal tower, platform, safe zone; metal textures; all CC0) |

## Art and music in this repository

| File | Author | Origin |
|---|---|---|
| `SourceArt/UI/T_MenuBackground1.png` | Edgaras "Gregaras" Ziaugra | Mobile Forces Source menu background (`background02.vtf`), whole; Valve's "source" logo in the corner replaced by an "unreal" box (`Tools/artgen/mfs_backgrounds.py`) |
| `SourceArt/UI/T_MenuBackground2.png` | Edgaras "Gregaras" Ziaugra | Mobile Forces Source menu background (`background01.vtf`), the same. A screenshot of an MFS map: some of its textures may come from the Source SDK - replace it if that matters |
| `SourceArt/Music/GameStartup.mp3` | Dawid Edward Kmita | Mobile Forces Source start-up theme (`gamestartup1.mp3`) |
| `SourceArt/Mods/Icons/*.bmp` | fistodul | MF-mods HUD icons |
| `Tools/artgen` output (crate) | generated | procedural, made for this project |

## fistodul (formerly World Gamers, the Mobile Forces Source team)

Filip Stojanovic, Junaid Malik (code); Filip Stojanovic, Edgaras Ziaugra, Ahmed Isaac Alizai
(maps); Edgaras Ziaugra, Augustinas Jatutas (materials); Edgaras Ziaugra (models); Dawid Edward
Kmita (sounds, music).

## Licence status of ported mod code

The MF-mods are re-written in C++ here, not copied, but a port written from someone's code is
still based on it. Gregaras' files (battleRoyale, arenaMode, mutPack, vote, his jeep / boat /
heli / piper edits) asked for GPL 3 "if legally possible"; GPL code can't ship inside an Unreal
Engine game (UE EULA, "Non-Compatible Licenses"). On 21 October 2026 Gregaras agreed, on the
Mobile Forces Discord, to his work going into Desktop Forces under MPL-2.0 (which stays usable in
GPL projects, MPL 2.0 section 3.3). His models, textures and sounds are CC0.

## Maps

Converted maps keep their authors (shown in the game). Community maps whose authors agreed to be
part of Desktop Forces are listed with them in [`Tools/mfport/packs.toml`](Tools/mfport/packs.toml).
The stock maps and all other original content come from your own copy of Mobile Forces through
the Mobile Forces Classic plugin and are not part of Desktop Forces.

## Thanks

Everyone keeping Mobile Forces alive, and Rage Software for the original game.

## Unreal Engine

Desktop Forces uses the Unreal® Engine. Unreal® is a trademark or registered trademark of Epic
Games, Inc. in the United States of America and elsewhere. Unreal® Engine, Copyright 1998 – 2026,
Epic Games, Inc. All rights reserved.
