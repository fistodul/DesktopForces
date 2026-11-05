#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/DFTypes.h"
#include "Weapons/DFWeapon.h"
#include "SentryGun.generated.h"

class ADFCharacter;
class UBoxComponent;
class UStaticMeshComponent;

/** 'SentryGunDOTSentryGun' - "%k's %w shredded %o." */
UCLASS()
class DESKTOPFORCES_API UDFDamage_Sentry : public UDFDamageType {
	GENERATED_BODY()
public:
	UDFDamage_Sentry();
};

/**
 * Deployed sentry turret (SentryGun.SentryGunTurret): looks for the nearest enemy in sight
 * within 1900 uu, turns at 95 deg/s and fires 20-damage shots, spinning up from 0.24 s to
 * 0.15 s between shots; 850 rounds, then it winds down and is gone. 80 health, blows up
 * (85, radius 250). 8 minutes. Timer driven: 5 Hz while scanning, 20 Hz while firing.
 */
UCLASS()
class DESKTOPFORCES_API ASentryTurret : public AActor {
	GENERATED_BODY()

public:
	uint8 GetTeam() const { return Team; }
	ASentryTurret();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	void InitPlacer(ADFCharacter* Placer);

	UPROPERTY(EditDefaultsOnly, Category = "Sentry")
	int32 MaxAmmo = 850;
	UPROPERTY(EditDefaultsOnly, Category = "Sentry")
	float ScanRange = 4275.f; // MF 1900 uu
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
	float TurnRate = 95.f; // degrees per second
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
	uint8 Team = DF::NoTeam;
	/** The placer by player, not pawn: still friendly to them after they respawn. */
	TWeakObjectPtr<class APlayerState> PlacerState;

	enum class ESound : uint8 { Activating,
		Fire,
		SpinDown };
	/** MF PlaySound: heard by everyone, not just the server. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSound(uint8 Which);
	/** SpawnTracer + AddWallHit: what each shot shows. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShot(FVector_NetQuantize Start, FVector_NetQuantize End, FVector_NetQuantizeNormal Normal, bool bWall);

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
class DESKTOPFORCES_API ASentryGun : public ADFWeapon {
	GENERATED_BODY()
public:
	ASentryGun();
	UPROPERTY(EditDefaultsOnly, Category = "Sentry")
	int32 MaxActiveSentries = 2;

	virtual void ForceInactive() override;

protected:
	virtual bool TryFire(bool bAlt) override;
	void Place();
	bool Deploy();
	void PlayArms(FName Name, float Duration);
	FTimerHandle PlaceTimer;
	float PlaceTime = 1.037f; // TripBombs PlaceForward 0.93 s at 0.9
	float PlaceBackTime = 0.444f; // PlaceBack / PlaceBombDown 0.4 s at 0.9
	bool FindPlacementSpot(FVector& OutLoc, FRotator& OutRot) const;
	void Message(const FString& Text) const;
};
