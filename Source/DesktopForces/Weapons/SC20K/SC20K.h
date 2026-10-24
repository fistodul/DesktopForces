#pragma once

#include "CoreMinimal.h"
#include "Weapons/MachineGun/MachineGun.h"
#include "SC20K.generated.h"

/**
 * SC-20K (the community servers' weapon mods, SC20K.u: authors / model sources still being
 * checked, so its model, icon and sounds come from the Mobile Forces Unverified pack and it is
 * only offered where that pack is installed). 28 damage, 70 to the head; 35 x 8.
 */
UCLASS()
class DESKTOPFORCES_API ASC20K : public AMachineGun {
	GENERATED_BODY()
public:
	ASC20K();
};
