#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/MFTypes.h"
#include "Weapons/AdrenalineShot/MFWeapon_AdrenalineShot.h"
#include "MFBattleRoyaleActors.generated.h"

class UBoxComponent;
class USphereComponent;
class UStaticMeshComponent;

/** Outside the Battle Royale safe zone ('safeZoneDamage'). */
UCLASS()
class DESKTOPFORCES_API UMFDamage_SafeZone : public UMFDamageType
{
	GENERATED_BODY()
};

/** itemBox: a crate that gives out five random weapons the first time someone touches it. */
UCLASS()
class DESKTOPFORCES_API AMFBRItemBox : public AActor
{
	GENERATED_BODY()

public:
	AMFBRItemBox();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool IsOpened() const { return bOpened; }

	UPROPERTY(EditAnywhere, Category = "BattleRoyale")
	int32 ItemCount = 5;

protected:
	UFUNCTION()
	void OnTouch(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);
	UFUNCTION()
	void OnRep_Opened();

	UPROPERTY(VisibleAnywhere, Category = "BattleRoyale")
	TObjectPtr<UBoxComponent> Box;
	UPROPERTY(VisibleAnywhere, Category = "BattleRoyale")
	TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(ReplicatedUsing = OnRep_Opened)
	bool bOpened = false;
};

/** metalTower: in the way, shootable; every 50 damage drops builder ammo. */
UCLASS()
class DESKTOPFORCES_API AMFBRTower : public AActor
{
	GENERATED_BODY()

public:
	AMFBRTower();
	virtual void BeginPlay() override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UPROPERTY(EditAnywhere, Category = "BattleRoyale")
	int32 Health = 500;

	/** Damage per builder ammo pickup dropped (0: none). */
	UPROPERTY(EditAnywhere, Category = "BattleRoyale")
	int32 DamageForAmmo = 50;

protected:
	virtual void ApplyLook();

	UPROPERTY(VisibleAnywhere, Category = "BattleRoyale")
	TObjectPtr<UStaticMeshComponent> Mesh;
	int32 AmmoDamage = 0;
};

/** platform: what the builder puts up (a tower without ammo inside). */
UCLASS()
class DESKTOPFORCES_API AMFBRPlatform : public AMFBRTower
{
	GENERATED_BODY()

public:
	AMFBRPlatform();

protected:
	virtual void ApplyLook() override;
};

/** builderAmmo: gives the builder, or 5 more platforms for it. */
UCLASS()
class DESKTOPFORCES_API AMFBRBuilderAmmo : public AActor
{
	GENERATED_BODY()

public:
	AMFBRBuilderAmmo();
	virtual void BeginPlay() override;

protected:
	UFUNCTION()
	void OnTouch(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UPROPERTY(VisibleAnywhere, Category = "BattleRoyale")
	TObjectPtr<USphereComponent> Sphere;
	UPROPERTY(VisibleAnywhere, Category = "BattleRoyale")
	TObjectPtr<UStaticMeshComponent> Mesh;
};

/** builder: fire puts up a platform 300 uu along the view; alt fire turns it flat / ramp / wall. */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_Builder : public AMFWeapon_AdrenalineShot
{
	GENERATED_BODY()

public:
	AMFWeapon_Builder();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnEquip() override;
	virtual void OnPutDown() override;
	virtual void Destroyed() override;
	void AddPlatforms(int32 Count);

	/** Platform pitch per alt fire step (MF 0, 4096, 16384: flat, ramp, wall). */
	static constexpr float Pitches[3] = { 0.f, 22.5f, 90.f };

protected:
	virtual bool TryFire(bool bAlt) override;
	FTransform GetPlacement() const;

	UPROPERTY(Replicated)
	int32 Angle = 0;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Preview;
};

/** safeZone: a shrinking sphere; soldiers outside lose health every second. */
UCLASS()
class DESKTOPFORCES_API AMFBRSafeZone : public AActor
{
	GENERATED_BODY()

public:
	AMFBRSafeZone();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool IsInside(const FVector& Where) const { return FVector::DistSquared(Where, GetActorLocation()) <= FMath::Square(Radius); }

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "BattleRoyale")
	float Radius = 45000.f;
	float StartRadius = 45000.f;
	float MinRadius = 1125.f;
	float ShrinkRate = 67.5f;     // cm/s
	int32 OutsideDamage = 5;      // per second
	bool bShrinking = false;

protected:
	void HurtOutside();

	UPROPERTY(VisibleAnywhere, Category = "BattleRoyale")
	TObjectPtr<UStaticMeshComponent> Mesh;
	float MeshRadius = 1.f;
	FTimerHandle HurtTimer;
};
