#pragma once

#include "CoreMinimal.h"
#include "Weapons/MachineGun/MachineGun.h"
#include "AK74.generated.h"

/**
 * AK-74 (the community servers' weapon mods, AK74.u: authors / model sources still being
 * checked, so its model, icon and sounds come from the Mobile Forces Unverified pack and it is
 * only offered where that pack is installed). 25 damage, 50 to the head; 30 x 8.
 */
UCLASS()
class DESKTOPFORCES_API AAK74 : public AMachineGun {
	GENERATED_BODY()
public:
	AAK74();
};
