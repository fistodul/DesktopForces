#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DFShellCase.generated.h"

/**
 * A spent case thrown out of a gun (RageRifle_BulletCase and its MachineGun / Pistol kin):
 * spins out, bounces up to three times with a tink (MiscSFX.Shell), then lies still a while.
 * Local only.
 */
UCLASS(NotPlaceable, Transient)
class DESKTOPFORCES_API ADFShellCase : public AActor {
	GENERATED_BODY()

public:
	ADFShellCase();
	/** Mesh: a converted prop ("RageWeapons/MachineGunBullet"); View: the shooter's eyes. */
	static void Eject(UWorld* World, const FString& Mesh, float Scale, const FVector& Eye, const FRotator& View, const FVector& InheritVelocity);

protected:
	UFUNCTION()
	void OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity);
	UFUNCTION()
	void OnStop(const FHitResult& Hit);

	UPROPERTY()
	TObjectPtr<class UStaticMeshComponent> Mesh;
	UPROPERTY()
	TObjectPtr<class UProjectileMovementComponent> Movement;
	int32 Bounces = 0;
};
