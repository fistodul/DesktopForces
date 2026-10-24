#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapon.h"
#include "Grenades.generated.h"

/** Grenades: hold to wind up (ThrowPower 1.5..10, 8/s), release to throw at power*150 uu/s. Alt = impact grenade. */
UCLASS()
class DESKTOPFORCES_API AGrenades : public ADFWeapon {
	GENERATED_BODY()
public:
	AGrenades();
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
