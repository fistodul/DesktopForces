#pragma once

#include "CoreMinimal.h"
#include "Weapons/MFWeapon.h"
#include "MFWeapon_MachineGun.generated.h"

/** MachineGun: 20 dmg, 48 x 5, ~8.6 rounds/s, alt = aim 2x (single shots). */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_MachineGun : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_MachineGun();
};
