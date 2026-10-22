#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapon.h"
#include "Pistol.generated.h"

/** Pistol: 26 dmg (head 40), 8 x 6, alt = aim 1.5x. */
UCLASS()
class DESKTOPFORCES_API APistol : public ADFWeapon
{
	GENERATED_BODY()
public:
	APistol();
};
