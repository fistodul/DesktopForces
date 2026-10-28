#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapon.h"
#include "Shotgun.generated.h"

/**
 * Shotgun: two barrels of 10 pellets x 12 dmg. Fire = one barrel, alt = both (two shells; one
 * when only one is left). MF fired a barrel per button and both when the buttons came within
 * 0.1 s; its barrel bookkeeping capped the clip at two, which is why ZombieGame had its own
 * shotgun. Here any clip works (infinite clips too) and the boomstick is an option
 * (?Boomstick, ADFGameState::bBoomstick).
 */
UCLASS()
class DESKTOPFORCES_API AShotgun : public ADFWeapon {
	GENERATED_BODY()
public:
	AShotgun();

protected:
	virtual void BeginPlay() override;
	virtual bool TryFire(bool bAlt) override;
	virtual void FireShot(bool bAlt) override;
	virtual void PlayFireEffects(bool bAlt) override;
	/** MF-mods ZombieShotgun ("Zombie Boomstick"): 12 pellets x 15, 22 clips, less kick. */
	void ApplyBoomstick();
};
