#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapon.h"
#include "HeavyMachineGun.generated.h"

/**
 * Heavy machine gun: alt deploys it on the ground (about 1 s, can't move while deployed),
 * then fire spins up from 0 to 24 rounds/s (MF: anim rate +1/s up to 3, 8 rounds per cycle),
 * spread widening 0.6 -> 1.2 with the spin. 45 damage, 900 rounds. Only fires deployed.
 */
UCLASS()
class DESKTOPFORCES_API AHeavyMachineGun : public ADFWeapon {
	GENERATED_BODY()
public:
	AHeavyMachineGun();
	virtual bool IsDeployed() const override { return ZoomLevel > 0; }
	virtual void StartFire(bool bAlt) override;
	virtual void StopFire(bool bAlt) override;
	virtual void ForceInactive() override;
	virtual FString GetHUDStatus() const override;
	virtual FName GetArmsAnimName(EDFWeaponState ForState, bool bAlt) override;

protected:
	virtual void PostInitializeComponents() override;
	virtual void ToggleZoom() override;
	/** HeavyMachineGunTripod: deployed, the tripod stands under the gun (for the others). */
	virtual void OnZoomChanged() override;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Tripod3P;
	void PlayArms(FName Name, float Rate);
	void SpinTick();
	FTimerHandle SpinTimer;
	FTimerHandle DeploySoundTimer;
	float SpinRate = 0.f; // MF FireAnimRate
	float ShotAccumulator = 0.f;
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float SpinAccel = 1.f; // MF AnimAccelRate
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float MaxSpin = 3.f; // MF MaxFireAnimRate
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float MinSpin = 0.4f; // MF MinFireAnimRate
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float RoundsPerCycle = 8.f;
};
