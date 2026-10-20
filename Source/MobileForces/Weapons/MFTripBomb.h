#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/MFTypes.h"
#include "Weapons/MFProjectile.h"
#include "MFTripBomb.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class AMFCharacter;

/**
 * Planted trip bomb (MF TripBomb). Arms after 1 s: the laser runs along the surface normal
 * to the first thing in the way (max 1024 uu). Anything breaking the beam - the planter
 * included - detonates it: 200 damage at the break point, radius 400. Shooting it: 120.
 * The beam is checked at 20 Hz by a timer (MF checked every tick).
 */
UCLASS()
class MOBILEFORCES_API AMFTripBomb : public AActor
{
	GENERATED_BODY()

public:
	AMFTripBomb();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	float MaxLaserDistance = 2304.f;
	/** Planter's team when it was planted (beam colour, mods: friendly checks). */
	UPROPERTY(Replicated)
	uint8 Team = MF::NoTeam;

	/** Same side as the planter: their team in team games, only the planter otherwise. */
	bool IsFriendlyTo(const AActor* Other) const;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Body;

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
	void UpdateBeam();

	FTimerHandle Timer;
	bool bExploded = false;
};

/** Thrown trip bomb (MF TripBombThrown): flies, then becomes a proximity mine where it lands. */
UCLASS()
class MOBILEFORCES_API AMFProjectile_TripMine : public AMFProjectile
{
	GENERATED_BODY()
public:
	AMFProjectile_TripMine();
protected:
	virtual void BeginPlay() override;
	void CheckProximity();
	/** A live pawn came close: does it set the mine off? */
	virtual bool ShouldTrigger(const AMFCharacter* Who) const { return true; }
	/** Set off by Who (default: explode). */
	virtual void Trigger(AMFCharacter* Who);
	FTimerHandle ProximityTimer;
	/** MF TripBombOnGround.CollisionRadius 64 uu. */
	float TriggerRadius = 144.f;
};
