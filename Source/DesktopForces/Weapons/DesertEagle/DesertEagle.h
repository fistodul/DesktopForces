#pragma once

#include "CoreMinimal.h"
#include "Weapons/Pistol/Pistol.h"
#include "DesertEagle.generated.h"

/**
 * Desert Eagle (the community servers' weapon mods, desert.u: authors / model sources still being
 * checked, so its model, icon and sounds come from the Mobile Forces Unverified pack and it is
 * only offered where that pack is installed). 25 damage, 70 to the head; 12 x 5.
 */
UCLASS()
class DESKTOPFORCES_API ADesertEagle : public APistol
{
	GENERATED_BODY()
public:
	ADesertEagle();
};
