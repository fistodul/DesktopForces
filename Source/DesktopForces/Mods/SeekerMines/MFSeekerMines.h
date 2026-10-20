#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Core/MFTypes.h"
#include "Weapons/MFProjectile.h"
#include "Weapons/MFWeapons.h"
#include "MFSeekerMines.generated.h"

class AMFCharacter;
class UFloatingPawnMovement;
class USphereComponent;
class UStaticMeshComponent;

/** 'SeekerMinesDOTSeekerMines' - "%k's %w hunted down %o." */
UCLASS()
class DESKTOPFORCES_API UMFDamage_SeekerMine : public UMFDamageType
{
	GENERATED_BODY()
public:
	UMFDamage_SeekerMine();
};

/**
 * Seeker mine drone (SeekerMines.SeekerMine): hovers after the nearest enemy within 1900 uu
 * (navmesh paths, so it goes around walls) and blows up next to them (320, radius 440).
 * With nobody to hunt it escorts its owner, or roams. 15 health; 8 minutes. Thinks at 5 Hz
 * on a timer, never ticks; movement is UFloatingPawnMovement driven by an AIController.
 */
UCLASS()
class DESKTOPFORCES_API AMFSeekerMine : public APawn
{
	GENERATED_BODY()

public:
	AMFSeekerMine();
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Owner and team at the moment it was thrown (InitPlacer). */
	void InitPlacer(AMFCharacter* Placer);

	UPROPERTY(EditDefaultsOnly, Category = "SeekerMine")
	float SeekRadius = 4275.f;        // MF 1900 uu
	UPROPERTY(EditDefaultsOnly, Category = "SeekerMine")
	float Damage = 320.f;
	UPROPERTY(EditDefaultsOnly, Category = "SeekerMine")
	float DamageRadius = 990.f;       // MF 440 uu
	UPROPERTY(EditDefaultsOnly, Category = "SeekerMine")
	float DetonateRange = 292.5f;     // MF 130 uu
	UPROPERTY(EditDefaultsOnly, Category = "SeekerMine")
	int32 Health = 15;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "SeekerMine")
	TObjectPtr<USphereComponent> Collision;
	UPROPERTY(VisibleAnywhere, Category = "SeekerMine")
	TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere, Category = "SeekerMine")
	TObjectPtr<UFloatingPawnMovement> Movement;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body, bool bSweep, const FHitResult& Hit);

	bool IsFriendly(const AActor* Other) const;
	AMFCharacter* FindBestTarget() const;
	void Think();
	void MoveTo(AActor* Goal, float Acceptance, float SpeedScale);
	void StartDetonating();
	void BlowUp();

	uint8 PlacedTeam = MF::NoTeam;
	bool bDetonating = false;
	bool bDetonated = false;
	float RoamUntil = 0.f;
	float BaseSpeed = 0.f;
	FTimerHandle ThinkTimer;
};

/** Thrown seeker mine (SeekerMineThrown): turns into the drone half a second later or on touching anything. */
UCLASS()
class DESKTOPFORCES_API AMFSeekerMineThrown : public AMFProjectile
{
	GENERATED_BODY()
public:
	AMFSeekerMineThrown();
protected:
	virtual void Explode(const FVector& Location, const FVector& Normal) override;
};

/** Seeker Mines (SeekerMines extends TripBombs): fire or alt fire winds up the throw; 1 per pick, 3 out per player. */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_SeekerMines : public AMFWeapon_TripBombs
{
	GENERATED_BODY()
public:
	AMFWeapon_SeekerMines();
	virtual void StartFire(bool bAlt) override;
	virtual void StopFire(bool bAlt) override;
	UPROPERTY(EditDefaultsOnly, Category = "SeekerMines")
	int32 MaxActiveMines = 3;
protected:
	virtual bool CanPlace() const override;
	virtual float GetThrowSpeed(float Power) const override { return 1800.f + Power * 405.f; }   // MF 800 + ThrowPower * 180 uu/s
};
