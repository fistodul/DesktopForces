#pragma once

#include "CoreMinimal.h"
#include "Weapons/MFWeapon.h"
#include "MFWeapon_Pistol.generated.h"

/** Pistol: 26 dmg (head 40), 8 x 6, alt = aim 1.5x. */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_Pistol : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_Pistol();
};
