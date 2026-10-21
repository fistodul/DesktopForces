#pragma once

#include "CoreMinimal.h"
#include "Weapons/MFWeapons.h"
#include "MFZombieWeapons.generated.h"

/*
 * Zombie mode's item variants (MF-mods ZombieGame by Animeman). The mode swaps them in for the
 * stock items (AMFGameMode::GetWeaponClassFor); small tweaks of the stock classes.
 */

/** ZombieKnife: slash 67 at 95 uu (a nemesis slashes twice as hard). */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_ZombieKnife : public AMFWeapon_Knife
{
	GENERATED_BODY()
public:
	AMFWeapon_ZombieKnife();
protected:
	virtual void FireShot(bool bAlt) override;
};

/** ZombieShotgun ("Zombie Boomstick"): both buttons fire one blast of 12 pellets x 15. */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_ZombieShotgun : public AMFWeapon_Shotgun
{
	GENERATED_BODY()
public:
	AMFWeapon_ZombieShotgun();
	virtual void StartFire(bool bAlt) override;
protected:
	virtual bool TryFire(bool bAlt) override;
};

/** ZombieShot: adrenaline that also turns an enemy within reach (100 health or less) into one of yours. */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_ZombieShot : public AMFWeapon_AdrenalineShot
{
	GENERATED_BODY()
public:
	AMFWeapon_ZombieShot();
protected:
	virtual bool TryFire(bool bAlt) override;
};

/** ZombieArmour: 120 charge, absorbs 80%. */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_ZombieArmour : public AMFWeapon_Armour
{
	GENERATED_BODY()
public:
	AMFWeapon_ZombieArmour();
};
