#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapon.h"
#include "AdrenalineShot.generated.h"

/** Adrenaline shot: restores full health; alt injects a team mate within 90 uu (on an enemy,
 *  the mode may do its own thing: ADFGameMode::InjectEnemy, Zombie mode's ZombieShot). */
UCLASS()
class DESKTOPFORCES_API AAdrenalineShot : public ADFWeapon {
	GENERATED_BODY()
public:
	AAdrenalineShot();

protected:
	virtual bool TryFire(bool bAlt) override;
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float InjectRange = 202.5f; // MF Range 90
};
