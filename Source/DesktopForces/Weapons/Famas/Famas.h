#pragma once

#include "CoreMinimal.h"
#include "Weapons/MachineGun/MachineGun.h"
#include "Famas.generated.h"

/**
 * FAMAS (the community servers' weapon mods, famas.u: authors / model sources still being
 * checked, so its model, icon and sounds come from the Mobile Forces Unverified pack and it is
 * only offered where that pack is installed). 28 damage, 56 to the head; 25 x 10.
 */
UCLASS()
class DESKTOPFORCES_API AFamas : public AMachineGun
{
	GENERATED_BODY()
public:
	AFamas();
};
