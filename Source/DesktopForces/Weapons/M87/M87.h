#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapon.h"
#include "M87.generated.h"

/**
 * M1887 (the community servers' weapon mods, m87.u: authors / model sources still being
 * checked, so its model, icon and sounds come from the Mobile Forces Unverified pack and it is
 * only offered where that pack is installed). 14 damage, 28 to the head; 8 x 8.
 */
UCLASS()
class DESKTOPFORCES_API AM87 : public ADFWeapon {
	GENERATED_BODY()
public:
	AM87();
	virtual float GetMoveSpeedScale() const override;
	virtual void ForceInactive() override;

protected:
	virtual bool TryFire(bool bAlt) override;
	virtual void Finish() override;

private:
	/** State AltFiring: DefaultFOV += 1 while in it, and RagePlayer walks whenever the FOV isn't the
	 *  default one (bIsWalking), so the quick lever shot slows you to walking pace. */
	bool bLeverShot = false;
	void SetLeverShot(bool bOn);
};
