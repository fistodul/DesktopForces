#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapon.h"
#include "MachineGun.generated.h"

/** MachineGun: 20 dmg, 48 x 5, ~8.6 rounds/s, alt = aim 2x (single shots). */
UCLASS()
class DESKTOPFORCES_API AMachineGun : public ADFWeapon {
	GENERATED_BODY()
public:
	AMachineGun();
};
