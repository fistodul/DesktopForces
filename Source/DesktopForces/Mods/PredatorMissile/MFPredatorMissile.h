#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weapons/MFWeapon.h"
#include "MFPredatorMissile.generated.h"

class UAudioComponent;
class USphereComponent;
class UStaticMeshComponent;
class AMFWeapon_PredatorMissile;

/**
 * The missile (MF-mods PredatorMissile): drops from as high as the sky allows above its
 * guider, who flies it from its nose - steering follows the view (diving 5 - 89 degrees), Fire
 * held boosts (2.5x, turns slower). 1000 damage in 1400 uu. Bots aim it at the nearest enemy.
 */
UCLASS()
class DESKTOPFORCES_API AMFPredatorMissile : public AActor
{
	GENERATED_BODY()

public:
	AMFPredatorMissile();
	virtual void Tick(float DeltaSeconds) override;
	virtual void LifeSpanExpired() override;
	virtual void Destroyed() override;

	/** Server, right after spawning: CeilingHeight = free air above the guider (cm). */
	void InitializeFlight(AMFWeapon_PredatorMissile* Weapon, float CeilingHeight);
	void Explode(const FVector& At);

	bool IsBoosting() const { return bBoosting; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<USphereComponent> Collision;
	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> FlySound;

	UPROPERTY(Replicated)
	bool bBoosting = false;

	TWeakObjectPtr<AMFWeapon_PredatorMissile> Controller;
	float BaseSpeed = 630.f;
	float CurrentSpeed = 315.f;
	float FlightTime = 0.f;
	bool bExploded = false;

	// MF defaults (cm where MF had uu)
	float InitialSlowDuration = 1.5f;
	float InitialSpeedFactor = 0.5f;
	float MinDiveAngle = 5.f;
	float MaxDiveAngle = 89.f;
	float TurnRateNormal = 120.f;     // deg/s
	float TurnRateBoost = 75.f;
	float TargetFlightTime = 4.5f;
	float BoostMultiplier = 2.5f;
	float BlastRadius = 3150.f;       // 1400 uu
	float Damage = 1000.f;

	AActor* FindBotTarget() const;
	FRotator ClampDive(FRotator R) const;

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

/**
 * Predator Missile terminal (MF-mods PredatorMissile): ready 10 s after you get it, one missile.
 * Fire: launch and guide it (you stand still meanwhile); it's used up when the missile is.
 */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_PredatorMissile : public AMFWeapon
{
	GENERATED_BODY()

public:
	AMFWeapon_PredatorMissile();
	virtual void PostInitializeComponents() override;
	virtual void OnGiven(AMFCharacter* NewOwner) override;
	virtual void ForceInactive() override;
	virtual void OnPutDown() override;
	virtual bool DrawHUD(UCanvas* Canvas) override;
	virtual FString GetHUDStatus() const override;
	virtual void Destroyed() override;

	/** Server: the missile is gone. */
	void MissileDone();
	AMFPredatorMissile* GetMissile() const { return ActiveMissile; }

protected:
	virtual bool TryFire(bool bAlt) override;
	bool FindLaunchLocation(FVector& OutLoc, FRotator& OutRot, float& OutCeiling) const;
	int32 WarmupSecondsLeft() const;

	UPROPERTY(Replicated)
	TObjectPtr<AMFPredatorMissile> ActiveMissile;

	/** Server world time when it can launch (MF WarmupDuration 10 s after GiveTo). */
	UPROPERTY(Replicated)
	float ReadyTime = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float WarmupDuration = 10.f;

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
