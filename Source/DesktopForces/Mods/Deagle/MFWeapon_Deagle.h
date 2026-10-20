#pragma once

#include "CoreMinimal.h"
#include "Weapons/MFWeapon_Pistol.h"
#include "MFWeapon_Deagle.generated.h"

/**
 * Desert Eagle (MF-mods deagle): a heavy pistol, 52 damage (head 120), 7 a clip, kicks hard.
 * Its model and sounds come from the mod's package when the classic content has them;
 * otherwise it borrows the pistol's model.
 */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_Deagle : public AMFWeapon_Pistol
{
	GENERATED_BODY()
public:
	AMFWeapon_Deagle();
	virtual void PostInitializeComponents() override;
};
