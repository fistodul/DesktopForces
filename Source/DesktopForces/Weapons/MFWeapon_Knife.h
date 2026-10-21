#pragma once

#include "CoreMinimal.h"
#include "Weapons/MFWeapon.h"
#include "MFWeapon_Knife.generated.h"

/** RageKnife: slash 30 dmg at 90 uu; alt throws the knife (60, head 250): walk over it to
 *  take it back, or it comes back by itself 6 s after it stops (Knife_Thrown state Safe). */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_Knife : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_Knife();
	virtual bool DrawHUD(UCanvas* Canvas) override { return false; }
	virtual bool IsMeleeWeapon() const override { return HasAmmoInClip(); }   // thrown away: not any more
protected:
	virtual void FireShot(bool bAlt) override;
	virtual void Finish() override;
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float MeleeRange = 202.5f;            // MF Range 90
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float KnifeReturnTime = 6.f;          // MF Knife_Thrown.TimeBeforeReturn
	FTimerHandle ReturnTimer;
public:
	/** The thrown knife is back in the hand (picked up, timed out or gone). */
	void KnifeReturned();
};
