#pragma once

#include "CoreMinimal.h"
#include "Weapons/Pistol/Pistol.h"
#include "Glock.generated.h"

/**
 * Glock 18 (the community servers' weapon mods, glock.u: authors / model sources still being
 * checked, so its model, icon and sounds come from the Mobile Forces Unverified pack and it is
 * only offered where that pack is installed). 28 damage, 40 to the head; 30 x 4.
 */
UCLASS()
class DESKTOPFORCES_API AGlock : public APistol {
	GENERATED_BODY()
public:
	AGlock();
};
