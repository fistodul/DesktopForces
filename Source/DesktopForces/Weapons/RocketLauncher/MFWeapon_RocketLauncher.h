#pragma once

#include "CoreMinimal.h"
#include "Weapons/MFWeapon.h"
#include "MFWeapon_RocketLauncher.generated.h"

/** Rocket launcher: 1 x 11 rockets (135 dmg, radius 400 uu); alt = 4x scope. TODO: lock-on SmartRocket. */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_RocketLauncher : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_RocketLauncher();
};
