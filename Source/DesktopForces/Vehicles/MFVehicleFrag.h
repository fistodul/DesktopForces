#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MFVehicleFrag.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class URotatingMovementComponent;

/**
 * A chunk a vehicle throws when it blows up (RageEffects.VehicleFrag): one of the nine carfrag
 * meshes, spinning, bouncing off walls with half its speed and clanking (FragHit1-3), lying
 * still once it lands; gone after 10-17 s. Cosmetic, spawned on each client (bNetTemporary).
 */
UCLASS(NotBlueprintable)
class DESKTOPFORCES_API AMFVehicleFrag : public AActor
{
	GENERATED_BODY()

public:
	AMFVehicleFrag();

	/** Vehicle.BlowUp: 16 frags flying out of a vehicle at Location, up to 4 of them burning
	 *  (EffectsFix.VehicleFragBurning, 2 in 5 while TrailFXC lasts). */
	static void Burst(UWorld* World, const FVector& Location);

	/** RageEffects.VehicleExplosion at DrawScale Scale: the fireball (Effects/P_VehicleExplosion)
	 *  and 2-4 ExplosionFires where the ground is under it. */
	static void Explosion(UWorld* World, const FVector& Location, float Scale = 1.f);

	/** RageEffects.ExplosionFire: flames and smoke for 4.5 s with the VehicleBurnLoop, at At or
	 *  following AttachTo. */
	static void Fire(UWorld* World, USceneComponent* AttachTo, const FVector& At, float Scale = 1.f);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity);

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<USphereComponent> Collision;
	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UProjectileMovementComponent> Movement;
	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<URotatingMovementComponent> Spin;
};
