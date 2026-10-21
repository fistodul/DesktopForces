#pragma once

/*
 * The Mobile Forces arsenal (RageWeapons package). Numbers come from the original
 * classes' defaultproperties / ProcessTraceHit; timings are the original animation
 * lengths: (NumFrames - 1) / (SeqRate * PlayRate) + TweenTime, read from RageWeapons.u
 * (Tools/mfport/mflib/anims.py prints them). Distances in cm (1 MF uu = 2.25 cm).
 */
// One folder per weapon (Weapons/<Name>/, MF's own and the MF-mods ones alike); this header
// pulls in the stock arsenal. The base classes: MFWeapon.h, MFProjectile.h.

#include "Weapons/Knife/MFWeapon_Knife.h"
#include "Weapons/Pistol/MFWeapon_Pistol.h"
#include "Weapons/MachineGun/MFWeapon_MachineGun.h"
#include "Weapons/Shotgun/MFWeapon_Shotgun.h"
#include "Weapons/Rifle/MFWeapon_Rifle.h"
#include "Weapons/RocketLauncher/MFWeapon_RocketLauncher.h"
#include "Weapons/Grenades/MFWeapon_Grenades.h"
#include "Weapons/HeavyMachineGun/MFWeapon_HeavyMachineGun.h"
#include "Weapons/TripBombs/MFWeapon_TripBombs.h"
#include "Weapons/AdrenalineShot/MFWeapon_AdrenalineShot.h"
#include "Weapons/Armour/MFWeapon_Armour.h"
