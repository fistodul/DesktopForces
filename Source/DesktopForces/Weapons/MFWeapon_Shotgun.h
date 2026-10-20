#pragma once

#include "CoreMinimal.h"
#include "Weapons/MFWeapon.h"
#include "MFWeapon_Shotgun.generated.h"

/**
 * Shotgun: two barrels of 10 pellets x 12 dmg. Fire = left barrel, alt = right barrel,
 * both buttons within 0.1 s = both barrels (MF WaitingFire/WaitingAltFire states).
 */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_Shotgun : public AMFWeapon
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
