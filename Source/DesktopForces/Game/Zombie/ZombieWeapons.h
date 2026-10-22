#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapons.h"
#include "ZombieWeapons.generated.h"

/*
 * Zombie mode's item variants (MF-mods ZombieGame by Animeman). The mode swaps them in for the
 * stock items (ADFGameMode::GetWeaponClassFor); small tweaks of the stock classes.
 */

/** ZombieKnife: slash 67 at 95 uu (a nemesis slashes twice as hard). */
UCLASS()
class DESKTOPFORCES_API AZombieKnife : public AKnife
{
	GENERATED_BODY()
public:
	AZombieKnife();
protected:
	virtual void FireShot(bool bAlt) override;
	/** ThrowKnife: 1000 (a tap) .. 2500 uu/s (held 1.25 s). */
	virtual float GetThrowSpeed(float Power) const override { return (1000.f + Power * Power * 15.f) * 2.25f; }
	virtual bool ShowsThrowPower() const override { return true; }
	/** ZombieKnife_Thrown: 158, a nemesis twice that; head x 3. */
	virtual void GetThrowDamage(float& Body, float& Head) const override;
	/** MyDamageType RageWeaponsDOTRocketLauncher: it blows vehicles up like a rocket. */
	virtual TSubclassOf<UDamageType> GetThrowDamageType() const override { return UDFDamage_Explosion::StaticClass(); }
};

/** ZombieShotgun ("Zombie Boomstick"): both buttons fire one blast of 12 pellets x 15. */
UCLASS()
class DESKTOPFORCES_API AZombieShotgun : public AShotgun
{
	GENERATED_BODY()
public:
	AZombieShotgun();
	virtual void StartFire(bool bAlt) override;
protected:
	virtual bool TryFire(bool bAlt) override;
};

/** ZombieShot: adrenaline that also turns an enemy within reach (100 health or less) into one of yours. */
UCLASS()
class DESKTOPFORCES_API AZombieShot : public AAdrenalineShot
{
	GENERATED_BODY()
public:
	AZombieShot();
protected:
	virtual bool TryFire(bool bAlt) override;
};

/** ZombieArmour: 120 charge, absorbs 80%. */
UCLASS()
class DESKTOPFORCES_API AZombieArmour : public AArmour
{
	GENERATED_BODY()
public:
	AZombieArmour();
};
