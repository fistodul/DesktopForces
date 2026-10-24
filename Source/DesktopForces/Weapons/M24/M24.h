#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapon.h"
#include "M24.generated.h"

/**
 * M24 (the community servers' weapon mods, M24.u: authors / model sources still being
 * checked, so its model, icon and sounds come from the Mobile Forces Unverified pack and it is
 * only offered where that pack is installed). 80 damage, 180 to the head; 8 x 6.
 */
UCLASS()
class DESKTOPFORCES_API AM24 : public ADFWeapon {
	GENERATED_BODY()
public:
	AM24();
};
