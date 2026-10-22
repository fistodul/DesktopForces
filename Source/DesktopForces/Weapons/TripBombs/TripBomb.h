#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/DFTypes.h"
#include "Weapons/DFProjectile.h"
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
class DESKTOPFORCES_API ATripBomb : public AActor
{
	GENERATED_BODY()

public:
	ATripBomb();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	float MaxLaserDistance = 2304.f;
	/** Planter's team when it was planted (beam colour, mods: friendly checks). */
	UPROPERTY(Replicated)
	uint8 Team = DF::NoTeam;

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
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastExplodeFX(FVector_NetQuantize At);
	void UpdateBeam();

	FTimerHandle Timer;
	bool bExploded = false;
};

/** Thrown trip bomb (MF TripBombThrown): flies, then becomes a proximity mine where it lands. */
UCLASS()
class DESKTOPFORCES_API ATripMine : public ADFProjectile
{
	GENERATED_BODY()
public:
	ATripMine();
protected:
	virtual void BeginPlay() override;
	void CheckProximity();
	/** A live pawn came close: does it set the mine off? */
	virtual bool ShouldTrigger(const ADFCharacter* Who) const { return true; }
	/** Set off by Who (default: explode). */
	virtual void Trigger(ADFCharacter* Who);
	FTimerHandle ProximityTimer;
	/** MF TripBombOnGround.CollisionRadius 64 uu. */
	float TriggerRadius = 144.f;
};
