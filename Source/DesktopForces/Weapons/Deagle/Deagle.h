#pragma once

#include "CoreMinimal.h"
#include "Weapons/Pistol/Pistol.h"
#include "Deagle.generated.h"

/**
 * Desert Eagle (MF-mods deagle): a heavy pistol, 52 damage (head 120), 7 a clip, kicks hard.
 * Its model and sounds come from the mod's package when the classic content has them;
 * otherwise it borrows the pistol's model.
 */
UCLASS()
class DESKTOPFORCES_API ADeagle : public APistol {
	GENERATED_BODY()
public:
	ADeagle();
	virtual void PostInitializeComponents() override;

protected:
	/** deagle.uc: the last round fires ShootEmpty (AccurateAimFireEmpty under the sights), a reload
	 *  from an empty clip plays ReloadEmpty with deagleEmptyReload. */
	virtual FName GetArmsAnimName(EDFWeaponState ForState, bool bAlt) override;
	virtual USoundBase* GetReloadSound() const override;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> EmptyReloadSound;
};
