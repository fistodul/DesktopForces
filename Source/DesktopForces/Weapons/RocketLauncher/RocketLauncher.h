#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapon.h"
#include "RocketLauncher.generated.h"

/**
 * Rocket launcher: 1 x 11 rockets (135 dmg, radius 400 uu); alt = 4x scope. Scoped, it locks on
 * (RocketLauncher.ZoomedIn): every 1.25 s it looks for an enemy within ~22 degrees of the view;
 * the same one twice running is locked (a beep; another when it's lost) and the next rocket is a
 * SmartRocket that steers at it.
 */
UCLASS()
class DESKTOPFORCES_API ARocketLauncher : public ADFWeapon
{
	GENERATED_BODY()
public:
	ARocketLauncher();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual FString GetHUDStatus() const override;

protected:
	virtual void ToggleZoom() override;
	virtual ADFProjectile* SpawnProjectile(TSubclassOf<ADFProjectile> Class, float Speed, bool bAlt) override;
	void CheckLock();
	/** RocketLauncher.CheckTarget: the enemy nearest the aim within the cone, in sight. */
	AActor* FindTarget() const;

	UPROPERTY(Replicated)
	TObjectPtr<AActor> LockedTarget;
	TWeakObjectPtr<AActor> LastSeen;
	FTimerHandle LockTimer;
};
