#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Core/DFTypes.h"
#include "Weapons/DFProjectile.h"
#include "Weapons/DFWeapons.h"
#include "Weapons/DFDevices.h"
#include "SeekerMines.generated.h"

class ADFCharacter;
class UFloatingPawnMovement;
class USphereComponent;
class UStaticMeshComponent;

/** 'SeekerMinesDOTSeekerMines' - "%k's %w hunted down %o." */
UCLASS()
class DESKTOPFORCES_API UDFDamage_SeekerMine : public UDFDamageType {
	GENERATED_BODY()
public:
	UDFDamage_SeekerMine();
};

/**
 * Seeker mine drone (SeekerMines.SeekerMine): hovers after the nearest enemy within 1900 uu
 * (navmesh paths, so it goes around walls) and blows up next to them (320, radius 440). Enemies
 * are soldiers, vehicles with an enemy inside, sentry turrets and other seeker mines (DF device
 * sides, DFDevices.h). With nobody to hunt it escorts its owner (up to them when over 450 uu
 * off, back to 250) or roams. Hums (TripBombs' Active) and pulses in its team's colour. 15 health;
 * 8 minutes. Thinks at 5 Hz on a timer, never ticks; movement is UFloatingPawnMovement driven
 * by an AIController.
 */
UCLASS()
class DESKTOPFORCES_API ASeekerMine : public APawn, public IDFDevice {
	GENERATED_BODY()

public:
	virtual uint8 GetPlacedTeam() const override { return PlacedTeam; }
	virtual const APlayerState* GetPlacer() const override { return PlacerState.Get(); }
	virtual bool IsWorking() const override { return !bDetonating && !bDetonated; }
	ASeekerMine();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Owner and team at the moment it was thrown (InitPlacer). */
	void InitPlacer(ADFCharacter* Placer);

	UPROPERTY(EditDefaultsOnly, Category = "SeekerMine")
	float SeekRadius = 4275.f; // MF 1900 uu
	UPROPERTY(EditDefaultsOnly, Category = "SeekerMine")
	float Damage = 320.f;
	UPROPERTY(EditDefaultsOnly, Category = "SeekerMine")
	float DamageRadius = 990.f; // MF 440 uu
	UPROPERTY(EditDefaultsOnly, Category = "SeekerMine")
	float DetonateRange = 292.5f; // MF 130 uu
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
	/** LightType LT_Pulse, LightRadius 12, hue 170 blue / 0 red / 40 nobody's (no shadows). */
	UPROPERTY(VisibleAnywhere, Category = "SeekerMine")
	TObjectPtr<class UPointLightComponent> Glow;
	UFUNCTION()
	void OnRep_PlacedTeam();
	void Pulse();
	FTimerHandle PulseTimer;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body, bool bSweep, const FHitResult& Hit);

	bool IsFriendly(const AActor* Other) const;
	AActor* FindBestTarget() const;
	/** state Escorting: up to the placer when over 450 uu off, back from them within 250. */
	void Escort(const ADFCharacter* Placer, float Dist);
	void Think();
	void MoveTo(AActor* Goal, float Acceptance, float SpeedScale);
	void StartDetonating();
	void BlowUp();

	UPROPERTY(ReplicatedUsing = OnRep_PlacedTeam)
	uint8 PlacedTeam = DF::NoTeam;
	TWeakObjectPtr<AController> Shooter; // who threw it: he may be dead when it finds someone
	TWeakObjectPtr<class APlayerState> PlacerState; // friendly to them after they respawn
	TWeakObjectPtr<AActor> Hunted; // a new hunt: the scan beep (Hunting.Scan PlaySound)
	bool bEscortClosing = false; // MoveToward the placer runs until it reaches them
	bool bDetonating = false;
	bool bDetonated = false;
	float RoamUntil = 0.f;
	float BaseSpeed = 0.f;
	FTimerHandle ThinkTimer;
};

/** Thrown seeker mine (SeekerMineThrown): turns into the drone half a second later or on touching anything. */
UCLASS()
class DESKTOPFORCES_API ASeekerMineThrown : public ADFProjectile {
	GENERATED_BODY()
public:
	ASeekerMineThrown();

protected:
	virtual void Explode(const FVector& Location, const FVector& Normal) override;
};

/** Seeker Mines (SeekerMines extends TripBombs): fire or alt fire winds up the throw; 1 per pick, 3 out per player. */
UCLASS()
class DESKTOPFORCES_API ASeekerMines : public ATripBombs {
	GENERATED_BODY()
public:
	ASeekerMines();
	virtual void StartFire(bool bAlt) override;
	virtual void StopFire(bool bAlt) override;
	UPROPERTY(EditDefaultsOnly, Category = "SeekerMines")
	int32 MaxActiveMines = 3;

protected:
	virtual bool CanPlace() const override;
	virtual float GetThrowSpeed(float Power) const override { return 1800.f + Power * 405.f; } // MF 800 + ThrowPower * 180 uu/s
	/** The buttons winding the throw up (bit 0 fire, bit 1 alt fire): it goes once both are up. */
	uint8 HeldButtons = 0;
};
