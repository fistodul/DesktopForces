#pragma once

#include "CoreMinimal.h"
#include "Weapons/MachineGun/MachineGun.h"
#include "G36.generated.h"

/**
 * G36 (the community servers' weapon mods, g36.u: authors / model sources still being
 * checked, so its model, icon and sounds come from the Mobile Forces Unverified pack and it is
 * only offered where that pack is installed). 24 damage, 48 to the head; 30 x 8.
 */
UCLASS()
class DESKTOPFORCES_API AG36 : public AMachineGun {
	GENERATED_BODY()
public:
	AG36();
};
