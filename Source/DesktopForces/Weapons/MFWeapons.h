#pragma once

/*
 * The Mobile Forces arsenal (RageWeapons package). Numbers come from the original
 * classes' defaultproperties / ProcessTraceHit; timings are the original animation
 * lengths: (NumFrames - 1) / (SeqRate * PlayRate) + TweenTime, read from RageWeapons.u
 * (Tools/mfport/mflib/anims.py prints them). Distances in cm (1 MF uu = 2.25 cm).
 */
// One file per weapon (MFWeapon_<Name>.h/.cpp); this header pulls in the whole arsenal.

#include "Weapons/MFWeapon_Knife.h"
#include "Weapons/MFWeapon_Pistol.h"
#include "Weapons/MFWeapon_MachineGun.h"
#include "Weapons/MFWeapon_Shotgun.h"
#include "Weapons/MFWeapon_Rifle.h"
#include "Weapons/MFWeapon_RocketLauncher.h"
#include "Weapons/MFWeapon_Grenades.h"
#include "Weapons/MFWeapon_HeavyMachineGun.h"
#include "Weapons/MFWeapon_TripBombs.h"
#include "Weapons/MFWeapon_AdrenalineShot.h"
#include "Weapons/MFWeapon_Armour.h"
