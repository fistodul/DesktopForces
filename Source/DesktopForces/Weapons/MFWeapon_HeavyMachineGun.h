#pragma once

#include "CoreMinimal.h"
#include "Weapons/MFWeapon.h"
#include "MFWeapon_HeavyMachineGun.generated.h"

/**
 * Heavy machine gun: alt deploys it on the ground (about 1 s, can't move while deployed),
 * then fire spins up from 0 to 24 rounds/s (MF: anim rate +1/s up to 3, 8 rounds per cycle),
 * spread widening 0.6 -> 1.2 with the spin. 45 damage, 900 rounds. Only fires deployed.
 */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_HeavyMachineGun : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_HeavyMachineGun();
	virtual void StartFire(bool bAlt) override;
	virtual void StopFire(bool bAlt) override;
	virtual void ForceInactive() override;
	virtual FString GetHUDStatus() const override;
protected:
	virtual void ToggleZoom() override;
	void SpinTick();
	FTimerHandle SpinTimer;
	float SpinRate = 0.f;       // MF FireAnimRate
	float ShotAccumulator = 0.f;
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float SpinAccel = 1.f;      // MF AnimAccelRate
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float MaxSpin = 3.f;        // MF MaxFireAnimRate
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float MinSpin = 0.4f;       // MF MinFireAnimRate
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float RoundsPerCycle = 8.f;
};
