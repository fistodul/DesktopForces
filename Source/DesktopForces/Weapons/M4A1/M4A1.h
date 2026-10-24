#pragma once

#include "CoreMinimal.h"
#include "Weapons/MachineGun/MachineGun.h"
#include "M4A1.generated.h"

/**
 * M4A1 (the community servers' weapon mods, m4a1.u: authors / model sources still being
 * checked, so its model, icon and sounds come from the Mobile Forces Unverified pack and it is
 * only offered where that pack is installed). 22 damage, 44 to the head; 30 x 8.
 */
UCLASS()
class DESKTOPFORCES_API AM4A1 : public AMachineGun {
	GENERATED_BODY()
public:
	AM4A1();
};
