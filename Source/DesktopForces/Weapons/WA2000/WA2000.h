#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapon.h"
#include "WA2000.generated.h"

/**
 * WA 2000 (the community servers' weapon mods, wa2000.u: authors / model sources still being
 * checked, so its model, icon and sounds come from the Mobile Forces Unverified pack and it is
 * only offered where that pack is installed). 60 damage, 10000 to the head; 6 x 5.
 */
UCLASS()
class DESKTOPFORCES_API AWA2000 : public ADFWeapon
{
	GENERATED_BODY()
public:
	AWA2000();
};
