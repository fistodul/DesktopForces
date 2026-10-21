#pragma once

#include "CoreMinimal.h"
#include "Weapons/MFWeapon.h"
#include "MFWeapon_AdrenalineShot.generated.h"

/** Adrenaline shot: restores full health; alt injects a team mate within 90 uu. */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_AdrenalineShot : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_AdrenalineShot();
protected:
	virtual bool TryFire(bool bAlt) override;
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float InjectRange = 202.5f;           // MF Range 90
};
