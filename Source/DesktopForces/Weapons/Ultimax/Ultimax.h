#pragma once

#include "CoreMinimal.h"
#include "Weapons/MachineGun/MachineGun.h"
#include "Ultimax.generated.h"

/**
 * Ultimax 100 (the community servers' weapon mods, ultimax.u: authors / model sources still being
 * checked, so its model, icon and sounds come from the Mobile Forces Unverified pack and it is
 * only offered where that pack is installed). 30 damage, 60 to the head; 100 x 4.
 */
UCLASS()
class DESKTOPFORCES_API AUltimax : public AMachineGun
{
	GENERATED_BODY()
public:
	AUltimax();
};
