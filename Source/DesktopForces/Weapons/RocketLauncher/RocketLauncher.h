#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapon.h"
#include "RocketLauncher.generated.h"

/** Rocket launcher: 1 x 11 rockets (135 dmg, radius 400 uu); alt = 4x scope. TODO: lock-on SmartRocket. */
UCLASS()
class DESKTOPFORCES_API ARocketLauncher : public ADFWeapon
{
	GENERATED_BODY()
public:
	ARocketLauncher();
};
