#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/MFTypes.h"
#include "Weapons/MFWeapon.h"
#include "MFSentryGun.generated.h"

class AMFCharacter;
class UBoxComponent;
class UStaticMeshComponent;

/** 'SentryGunDOTSentryGun' - "%k's %w shredded %o." */
UCLASS()
class DESKTOPFORCES_API UMFDamage_Sentry : public UMFDamageType
{
	GENERATED_BODY()
public:
	UMFDamage_Sentry();
};

/**
 * Deployed sentry turret (SentryGun.SentryGunTurret): looks for the nearest enemy in sight
 * within 1900 uu, turns at 95 deg/s and fires 20-damage shots, spinning up from 0.24 s to
 * 0.15 s between shots; 850 rounds, then it winds down and is gone. 80 health, blows up
 * (85, radius 250). 8 minutes. Timer driven: 5 Hz while scanning, 20 Hz while firing.
 */
UCLASS()
class DESKTOPFORCES_API AMFSentryTurret : public AActor
{
	GENERATED_BODY()

public:
	AMFSentryTurret();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	void InitPlacer(AMFCharacter* Placer);

	UPROPERTY(EditDefaultsOnly, Category = "Sentry")
	int32 MaxAmmo = 850;
	UPROPERTY(EditDefaultsOnly, Category = "Sentry")
	float ScanRange = 4275.f;          // MF 1900 uu
	UPROPERTY(EditDefaultsOnly, Category = "Sentry")
	float ShotDamage = 20.f;
	UPROPERTY(EditDefaultsOnly, Category = "Sentry")
	float FireInterval = 0.15f;
	UPROPERTY(EditDefaultsOnly, Category = "Sentry")
	float InitialFireInterval = 0.24f;
	UPROPERTY(EditDefaultsOnly, Category = "Sentry")
	float FireIntervalStep = 0.04f;
	UPROPERTY(EditDefaultsOnly, Category = "Sentry")
	float Spread = 0.025f;
	UPROPERTY(EditDefaultsOnly, Category = "Sentry")
	float TurnRate = 95.f;             // degrees per second
	UPROPERTY(EditDefaultsOnly, Category = "Sentry")
	int32 Health = 80;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Sentry")
	TObjectPtr<UBoxComponent> Collision;
	UPROPERTY(VisibleAnywhere, Category = "Sentry")
	TObjectPtr<UStaticMeshComponent> Tripod;
	UPROPERTY(VisibleAnywhere, Category = "Sentry")
	TObjectPtr<UStaticMeshComponent> Gun;

	/** Where the gun points (replicated so clients see it turn). */
	UPROPERTY(ReplicatedUsing = OnRep_Aim)
	FRotator Aim;
	UFUNCTION()
	void OnRep_Aim();

	UPROPERTY(Replicated)
	uint8 Team = MF::NoTeam;

	bool IsFriendly(const AActor* Other) const;
	bool CanSee(const AActor* Target) const;
	FVector Muzzle() const;
	AActor* FindBestTarget() const;
	void Scan();
	void FireTick();
	void FireShot();
	void OutOfAmmo();
	void BlowUp();
	AController* Credit() const;

	TWeakObjectPtr<AActor> Target;
	int32 Ammo = 0;
	float CurrentInterval = 0.f;
	float NextShot = 0.f;
	bool bDead = false;
	FTimerHandle Timer;
};

/** Sentry Gun (SentryGun extends TripBombs): fire or alt fire deploys it on flat ground in front; 2 out per player. */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_SentryGun : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_SentryGun();
	UPROPERTY(EditDefaultsOnly, Category = "Sentry")
	int32 MaxActiveSentries = 2;
protected:
	virtual bool TryFire(bool bAlt) override;
	bool FindPlacementSpot(FVector& OutLoc, FRotator& OutRot) const;
	void Message(const FString& Text) const;
};
