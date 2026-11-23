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

protected:
	UFUNCTION(Server, Reliable)
	void ServerThrow(bool bAlt, float Power);
	void Throw(bool bAlt, float Power);
	/** The button is up and the pin is out: throw (MF NormalFire / AltFiring Tick). */
	void Release();
	/** ThrowOver done: the next grenade comes up (MF PlayIdleAnim -> PlayReloadingAnim). */
	void NextGrenade();
	bool bWindingAlt = false;
	/** Let go before the pin was out: the throw waits for the pull (FinishAnim, then Tick). */
	bool bReleased = false;
	FTimerHandle PinTimer;
	/** Grenades.EjectPin: the pin flies off (Grenade_Pin, a bullet case), EjectPause into the pull. */
	void EjectPin(bool bTeeth);
	FTimerHandle EjectTimer;
	FTimerHandle NextTimer;
};
