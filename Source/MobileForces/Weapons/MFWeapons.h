#pragma once

#include "CoreMinimal.h"
#include "Weapons/MFWeapon.h"
#include "MFWeapons.generated.h"

/*
 * The Mobile Forces arsenal (RageWeapons package). Numbers come from the original
 * classes' defaultproperties / ProcessTraceHit; timings are the original animation
 * lengths: (NumFrames - 1) / (SeqRate * PlayRate) + TweenTime, read from RageWeapons.u
 * (Tools/mfport/mflib/anims.py prints them). Distances in cm (1 MF uu = 2.25 cm).
 */

/** RageKnife: slash 30 dmg at 90 uu; alt throws the knife (60, head 250), back after 6 s. */
UCLASS()
class MOBILEFORCES_API AMFWeapon_Knife : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_Knife();
	virtual bool DrawHUD(UCanvas* Canvas) override { return false; }
protected:
	virtual void FireShot(bool bAlt) override;
	virtual void Finish() override;
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float MeleeRange = 202.5f;            // MF Range 90
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float KnifeReturnTime = 6.f;          // MF Knife_Thrown.TimeBeforeReturn
	FTimerHandle ReturnTimer;
	void KnifeReturned();
};

/** Pistol: 26 dmg (head 40), 8 x 6, alt = aim 1.5x. */
UCLASS()
class MOBILEFORCES_API AMFWeapon_Pistol : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_Pistol();
};

/** MachineGun: 20 dmg, 48 x 5, ~8.6 rounds/s, alt = aim 2x (single shots). */
UCLASS()
class MOBILEFORCES_API AMFWeapon_MachineGun : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_MachineGun();
};

/**
 * Shotgun: two barrels of 10 pellets x 12 dmg. Fire = left barrel, alt = right barrel,
 * both buttons within 0.1 s = both barrels (MF WaitingFire/WaitingAltFire states).
 */
UCLASS()
class MOBILEFORCES_API AMFWeapon_Shotgun : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_Shotgun();
	virtual void StartFire(bool bAlt) override;
protected:
	virtual bool TryFire(bool bAlt) override;
	virtual void FireShot(bool bAlt) override;
	void FireAfterWindow();
	FTimerHandle WindowTimer;
	bool bWindowAlt = false;
	bool bTwin = false;
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float TwinFireWindow = 0.1f;
};

/**
 * Sniper rifle (RageRifle): hold fire to power up 1 -> 10 (2 per second, 4.5 s),
 * release to fire 20 x power damage (head x3). Alt toggles the scope (3x-30x, mouse wheel).
 */
UCLASS()
class MOBILEFORCES_API AMFWeapon_Rifle : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_Rifle();
	virtual void StartFire(bool bAlt) override;
	virtual void StopFire(bool bAlt) override;
	virtual float GetFOVScale() const override;
	virtual FString GetHUDStatus() const override;
	void AdjustZoom(int32 Steps);
	float GetShotPower() const;
protected:
	virtual void FireShot(bool bAlt) override;
	virtual void Finish() override;
	virtual void ProcessTraceHit(const FHitResult& Hit, const FVector& ShotDir, bool bAlt) override;
	UFUNCTION(Server, Reliable)
	void ServerReleaseFire(float Power);
	double ChargeStart = 0.0;
	bool bCharging = false;
	float ReleasedPower = 1.f;
	UPROPERTY(Replicated)
	float ZoomScaleAlpha = 0.f;
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float MinZoomScale = 3.f;
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float MaxZoomScale = 30.f;
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	int32 ZoomScaleIncrements = 6;
public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

/** Rocket launcher: 1 x 11 rockets (135 dmg, radius 400 uu); alt = 4x scope. TODO: lock-on SmartRocket. */
UCLASS()
class MOBILEFORCES_API AMFWeapon_RocketLauncher : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_RocketLauncher();
};

/** Grenades: hold to wind up (ThrowPower 1.5..10, 8/s), release to throw at power*150 uu/s. Alt = impact grenade. */
UCLASS()
class MOBILEFORCES_API AMFWeapon_Grenades : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_Grenades();
	virtual void StartFire(bool bAlt) override;
	virtual void StopFire(bool bAlt) override;
	virtual FString GetHUDStatus() const override;
protected:
	UFUNCTION(Server, Reliable)
	void ServerThrow(bool bAlt, float Power);
	void Throw(bool bAlt, float Power);
	double WindUpStart = 0.0;
	bool bWinding = false;
	bool bWindingAlt = false;
	float GetThrowPower() const;
};

/**
 * Heavy machine gun: alt deploys it on the ground (about 1 s, can't move while deployed),
 * then fire spins up from 0 to 24 rounds/s (MF: anim rate +1/s up to 3, 8 rounds per cycle),
 * spread widening 0.6 -> 1.2 with the spin. 45 damage, 900 rounds. Only fires deployed.
 */
UCLASS()
class MOBILEFORCES_API AMFWeapon_HeavyMachineGun : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_HeavyMachineGun();
	virtual void StartFire(bool bAlt) override;
	virtual void StopFire(bool bAlt) override;
	virtual void ForceInactive() override;
	virtual FString GetHUDStatus() const override;
protected:
	virtual void ToggleZoom() override;
	void SpinTick();
	FTimerHandle SpinTimer;
	float SpinRate = 0.f;       // MF FireAnimRate
	float ShotAccumulator = 0.f;
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float SpinAccel = 1.f;      // MF AnimAccelRate
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float MaxSpin = 3.f;        // MF MaxFireAnimRate
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float MinSpin = 0.4f;       // MF MinFireAnimRate
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float RoundsPerCycle = 8.f;
};

/**
 * Trip bombs: fire plants a bomb on the surface in front (within 50 uu); after 1 s its
 * laser spans to the opposite surface (max 1024 uu) and anything crossing it sets it off
 * (200 damage, radius 400). Alt winds up a throw (power 3-10, +5/s) and drops a proximity
 * mine (120 damage, radius 200, triggers within 64 uu). Two per item, max 3 items.
 */
UCLASS()
class MOBILEFORCES_API AMFWeapon_TripBombs : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_TripBombs();
	virtual void StartFire(bool bAlt) override;
	virtual void StopFire(bool bAlt) override;
	virtual FString GetHUDStatus() const override;
protected:
	UFUNCTION(Server, Reliable)
	void ServerUse(bool bAlt, float Power);
	void Use(bool bAlt, float Power);
	bool TryPlant();
	double WindUpStart = 0.0;
	bool bWinding = false;
	float GetThrowPower() const;
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float MaxLaserDistance = 2304.f;   // MF 1024
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float PlaceReach = 112.5f;         // MF trace 50 uu
};

/** Adrenaline shot: restores full health; alt injects a team mate within 90 uu. */
UCLASS()
class MOBILEFORCES_API AMFWeapon_AdrenalineShot : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_AdrenalineShot();
protected:
	virtual bool TryFire(bool bAlt) override;
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float InjectRange = 202.5f;           // MF Range 90
};

/** Body armour: passive, absorbs 75% of damage until its 100 charge is used up. */
UCLASS()
class MOBILEFORCES_API AMFWeapon_Armour : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_Armour();
	virtual void GiveFullAmmo() override;
	virtual int32 AbsorbDamage(int32 Damage, const UMFDamageType* Type) override;
	int32 GetCharge() const { return Charge; }
protected:
	virtual bool TryFire(bool bAlt) override { return false; }
	UPROPERTY(Replicated)
	int32 Charge = 100;
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	int32 MaxCharge = 100;
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	int32 ArmorAbsorption = 75;
public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
