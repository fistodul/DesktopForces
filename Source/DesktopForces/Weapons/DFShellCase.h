#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DFShellCase.generated.h"

/**
 * What a gun throws out (UE1 RageRifle_BulletCase and kin): Eject(((FRand()*R.X+Min.X)*X +
 * (FRand()*R.Y+Min.Y)*Y + (FRand()*R.Z+Min.Z)*Z) * Speed) along the shooter's view.
 */
USTRUCT(BlueprintType)
struct DESKTOPFORCES_API FDFCasing {
	GENERATED_BODY()

	/** A converted prop ("RageWeapons/MachineGunBullet"); empty: none. */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	FString Mesh;
	/** DrawScale. */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float Scale = 1.f;
	/** The view-axis factors' least values and random spans (RageRifle_BulletCase's). */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	FVector VelocityMin = FVector(0.4f, 0.2f, 1.f);
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	FVector VelocityRange = FVector(0.3f, 0.2f, 0.3f);
	/** cm/s (MF 160 uu/s). */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float Speed = 360.f;
	/** Bounces before it lies still (RageRifle_BulletCase 3; the jeep gun's vehWeap_Shell 2). */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	int32 MaxBounces = 3;
	/** Its tink, a sound under the content root. */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	FString Sound = TEXT("Sounds/MiscSFX/Shell");
};

/**
 * A spent case thrown out of a gun (RageRifle_BulletCase and its MachineGun / Pistol kin):
 * spins out, bounces a few times with a tink, then lies still a while. Local only.
 */
UCLASS(NotPlaceable, Transient)
class DESKTOPFORCES_API ADFShellCase : public AActor {
	GENERATED_BODY()

public:
	ADFShellCase();
	/** View: the shooter's eyes; Port: where it comes out, along the view's axes (cm); Side -1
	 *  mirrors it to the left (a grenade pin pulled by hand). */
	static void Eject(UWorld* World, const FDFCasing& Casing, const FVector& Eye, const FRotator& View, const FVector& Port, const FVector& InheritVelocity, float Side = 1.f);
	/** A bit thrown from At at Velocity (Casing: its mesh, scale, bounces, tink; RageChip off a wall). */
	static ADFShellCase* Launch(UWorld* World, const FDFCasing& Casing, const FVector& At, const FRotator& Rotation, const FVector& Velocity);

protected:
	UFUNCTION()
	void OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity);
	UFUNCTION()
	void OnStop(const FHitResult& Hit);

	UPROPERTY()
	TObjectPtr<class UStaticMeshComponent> Mesh;
	UPROPERTY()
	TObjectPtr<class UProjectileMovementComponent> Movement;
	UPROPERTY()
	TObjectPtr<class USoundBase> Tink;
	int32 Bounces = 0;
	int32 MaxBounces = 3;
};
