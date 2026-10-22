// Gregaras' breakable boxes (BoxPhysics.u). Code ported with his agreement (MPL-2.0); model and
// texture his (BoxPhysics package, core pack).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Vehicles/VehicleFrag.h"
#include "WoodenBox.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * BoxPhysics.Box: a wooden crate that is pushed around, knocked about by shots and explosions
 * (a physics body) and bursts into 15 planks once it has taken 100 damage. Most float: each box
 * gets a buoyancy of 0.9-1.5 times its weight (Box.PreBeginPlay). `summon WoodenBox`, or placed.
 */
UCLASS()
class DESKTOPFORCES_API AWoodenBox : public AActor
{
	GENERATED_BODY()

public:
	AWoodenBox();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UPROPERTY(EditAnywhere, Category = "DF")
	float Health = 100.f;
	/** Box.FragChunks / FragSize (planks DrawScale FragSize * 0.5-1.2). */
	UPROPERTY(EditAnywhere, Category = "DF")
	int32 FragChunks = 15;
	UPROPERTY(EditAnywhere, Category = "DF")
	float FragSize = 50.f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION()
	void OnRep_Broken();

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<UBoxComponent> Collision;
	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(ReplicatedUsing = OnRep_Broken)
	bool bBroken = false;
	/** Buoyancy / weight: above 1 it floats. */
	float Buoyancy = 1.f;
};

/** BoxPhysics.WoodFragments: a plank thrown out of a broken box (cosmetic, on each machine). */
UCLASS(NotBlueprintable)
class DESKTOPFORCES_API AWoodFragment : public AVehicleFrag
{
	GENERATED_BODY()

public:
	/** Decoration.Frag: Count planks from Location, sized Size * 0.5-1.2. */
	static void Burst(UWorld* World, const FVector& Location, int32 Count, float Size);

protected:
	virtual void PickMesh() override;
	virtual void PlayHitSound(float Speed) override {}   // no ImpactSound
};
