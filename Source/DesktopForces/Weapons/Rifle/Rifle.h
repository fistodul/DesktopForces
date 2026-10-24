#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapon.h"
#include "Rifle.generated.h"

/**
 * Sniper rifle (RageRifle): hold fire to power up 1 -> 10 (2 per second, 4.5 s),
 * release to fire 20 x power damage (head x3). Alt toggles the scope (3x-30x, mouse wheel).
 */
UCLASS()
class DESKTOPFORCES_API ARifle : public ADFWeapon {
	GENERATED_BODY()
public:
	ARifle();
	virtual void StartFire(bool bAlt) override;
	virtual void StopFire(bool bAlt) override;
	virtual float GetFOVScale() const override;
	virtual FString GetHUDStatus() const override;
	void AdjustZoom(int32 Steps);
	float GetShotPower() const;

protected:
	virtual void FireShot(bool bAlt) override;
	virtual void Finish() override;
	virtual void ProcessTraceHit(const FHitResult& Hit, const FVector& ShotDir, bool bAlt) override;
	UFUNCTION(Server, Reliable)
	void ServerReleaseFire(float Power);
	double ChargeStart = 0.0;
	bool bCharging = false;
	/** Full power reached: the Powering loop stops, PoweredUp plays. */
	FTimerHandle PoweredUpTimer;
	float ReleasedPower = 1.f;
	UPROPERTY(Replicated)
	float ZoomScaleAlpha = 0.f;
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float MinZoomScale = 3.f;
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float MaxZoomScale = 30.f;
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	int32 ZoomScaleIncrements = 6;

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
