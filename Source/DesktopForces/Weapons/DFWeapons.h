#pragma once

/*
 * The Mobile Forces arsenal (RageWeapons package). Numbers come from the original
 * classes' defaultproperties / ProcessTraceHit; timings are the original animation
 * lengths: (NumFrames - 1) / (SeqRate * PlayRate) + TweenTime, read from RageWeapons.u
 * (Tools/mfport/mflib/anims.py prints them). Distances in cm (1 MF uu = 2.25 cm).
 */
// One folder per weapon (Weapons/<Name>/, MF's own and the MF-mods ones alike); this header
// pulls in the stock arsenal. The base classes: DFWeapon.h, DFProjectile.h.

#include "Weapons/Knife/Knife.h"
#include "Weapons/Pistol/Pistol.h"
#include "Weapons/MachineGun/MachineGun.h"
#include "Weapons/Shotgun/Shotgun.h"
#include "Weapons/Rifle/Rifle.h"
#include "Weapons/RocketLauncher/RocketLauncher.h"
#include "Weapons/Grenades/Grenades.h"
#include "Weapons/HeavyMachineGun/HeavyMachineGun.h"
#include "Weapons/TripBombs/TripBombs.h"
#include "Weapons/AdrenalineShot/AdrenalineShot.h"
#include "Weapons/Armour/Armour.h"
