#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapon.h"
#include "Knife.generated.h"

/** RageKnife: slash 30 dmg at 90 uu; hold alt to pull the arm back, let go to throw it at
 *  1500 uu/s (60, head 250): it sticks in walls; walk over it to take it back, or it comes back
 *  by itself 6 s after it stops (Knife_Thrown state Safe). */
UCLASS()
class DESKTOPFORCES_API AKnife : public ADFWeapon
{
	GENERATED_BODY()
public:
	AKnife();
	virtual bool DrawHUD(UCanvas* Canvas) override { return false; }
	virtual bool IsMeleeWeapon() const override { return HasAmmoInClip(); }   // thrown away: not any more
	virtual void StartFire(bool bAlt) override;
	virtual void StopFire(bool bAlt) override;
	virtual FString GetHUDStatus() const override;
protected:
	virtual void FireShot(bool bAlt) override;
	virtual void PlayFireEffects(bool bAlt) override;
	virtual void Finish() override;
	/** Throw speed (cm/s) after holding for Power (0-10, +8 a second): RageKnife 10 * 150 uu/s. */
	virtual float GetThrowSpeed(float Power) const { return 3375.f; }
	/** Show the wind-up on the HUD (ZombieKnife.DrawPowerGuage). */
	virtual bool ShowsThrowPower() const { return false; }
	/** The thrown knife's damage and head damage (Knife_Thrown: 60, decapitated 250). */
	virtual void GetThrowDamage(float& Body, float& Head) const { Body = 60.f; Head = 250.f; }
	virtual TSubclassOf<UDamageType> GetThrowDamageType() const { return UDFDamage_Knife::StaticClass(); }
	UFUNCTION(Server, Reliable)
	void ServerThrow(float Power);
	void Throw(float Power);
	float GetThrowPower() const;
	double WindUpStart = 0.0;
	bool bWinding = false;
	bool bThrowQueued = false;
	FTimerHandle WindUpTimer;
	/** AltFiring: the arm goes back first (PlayAltFiringAnim) before a release can throw. */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float WindUpTime = 0.3f;
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float MeleeRange = 202.5f;            // MF Range 90
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float KnifeReturnTime = 6.f;          // MF Knife_Thrown.TimeBeforeReturn
	FTimerHandle ReturnTimer;
public:
	/** The thrown knife is back in the hand (picked up, timed out or gone). */
	void KnifeReturned();
};
