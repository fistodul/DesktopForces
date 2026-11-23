#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/DFTypes.h"
#include "Weapons/DFProjectile.h"
#include "Weapons/DFDevices.h"
#include "TripBomb.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class ADFCharacter;

/**
 * Planted trip bomb (MF TripBomb). Arms after 1 s: the laser runs along the surface normal
 * to the first thing in the way (max 1024 uu). Anything breaking the beam - the planter
 * included - detonates it: 200 damage at the break point, radius 400. Shooting it: 120.
 * The beam is checked at 20 Hz by a timer (MF checked every tick).
 */
UCLASS()
class DESKTOPFORCES_API ATripBomb : public AActor, public IDFDevice {
	GENERATED_BODY()

public:
	ATripBomb();
	virtual uint8 GetPlacedTeam() const override { return Team; }
	virtual const APlayerState* GetPlacer() const override { return PlacerState.Get(); }
	virtual bool IsWorking() const override { return !bExploded; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	float MaxLaserDistance = 2304.f;
	/** Planter's team when it was planted (beam colour, mods: friendly checks). */
	UPROPERTY(Replicated)
	uint8 Team = DF::NoTeam;

	/** TripLaser.IsFriendly: a team mate, a team's device, a vehicle with only friends in it (an
	 *  empty one is no friend: it stops the beam and sets it off); the placer only in deathmatch. */
	bool IsFriendlyTo(const AActor* Other) const;

protected:
	virtual void BeginPlay() override;
	TWeakObjectPtr<AController> Shooter;
	TWeakObjectPtr<APlayerState> PlacerState;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Body;
	/** RageWeapons' TripBombMesh (the mods' TripLaser too) on the wall; the box stays without it. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Model;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Beam;

	UPROPERTY(ReplicatedUsing = OnRep_Laser)
	FVector_NetQuantize LaserEnd;

	UFUNCTION()
	void OnRep_Laser();

	/** 1 s after planting: find where the beam ends, start watching it. */
	virtual void Arm();
	/** 20 Hz: something in the beam? */
	virtual void CheckBeam();
	void Detonate(const FVector& At, float Damage);
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastExplodeFX(FVector_NetQuantize At);
	void UpdateBeam();

	FTimerHandle Timer;
	bool bExploded = false;
};

/**
 * Thrown trip bomb (MF TripBombThrown): flies, then becomes a proximity mine where it lands,
 * lying on its base. An explosion near it, a shot or another thrown one landing on it sets it
 * off (a moment later: a heap of them goes up one after another).
 */
UCLASS()
class DESKTOPFORCES_API ATripMine : public ADFProjectile, public IDFDevice {
	GENERATED_BODY()
public:
	ATripMine();
	virtual uint8 GetPlacedTeam() const override { return PlacedTeam; }
	virtual const APlayerState* GetPlacer() const override { return PlacerState.Get(); }
	virtual bool IsWorking() const override { return !bExploded; }
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void BeginPlay() override;
	virtual void OnStop(const FHitResult& Hit) override;
	virtual void OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity) override;
	virtual AController* GetBlastCredit() const override;
	/** Thrown onto another mine: both go. */
	void HitOtherMine(AActor* Other);
	/** Set off by a blast or a hit: Explode a moment later (chains spread). */
	virtual void SetOff();
	/** The thrower's side when it was thrown. */
	uint8 PlacedTeam = DF::NoTeam;
	TWeakObjectPtr<APlayerState> PlacerState;
	FTimerHandle SetOffTimer;
	void CheckProximity();
	/** A live pawn came close: does it set the mine off? */
	virtual bool ShouldTrigger(const ADFCharacter* Who) const { return true; }
	/** Set off by Who (default: explode). */
	virtual void Trigger(ADFCharacter* Who);
	FTimerHandle ProximityTimer;
	/** MF TripBombOnGround.CollisionRadius 64 uu. */
	float TriggerRadius = 144.f;
	/** At rest, the sphere a knife or bullet hits it by (TripBombOnGround: bCollisionSphere,
	 *  CollisionRadius 64, bProjTarget): weapon traces only, made on landing. */
	UPROPERTY(Transient)
	TObjectPtr<class USphereComponent> HitSphere;
};
