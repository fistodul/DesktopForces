#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DFTracer.generated.h"

/**
 * A bullet tracer (RageWeapon.FireTracer / FakeTracer): a glowing streak flying from the gun to
 * where the shot went at 3000 uu/s. Local only, cheap: a thin box with an additive material
 * (M_MF_Tracer, made by the importer).
 */
UCLASS(NotPlaceable, Transient)
class DESKTOPFORCES_API ADFTracer : public AActor {
	GENERATED_BODY()

public:
	ADFTracer();
	/** Start at the gun, End where the shot hit (or its end). Nothing on a dedicated server. */
	static void Fire(UWorld* World, const FVector& Start, const FVector& End);
	virtual void Tick(float DeltaSeconds) override;

protected:
	UPROPERTY()
	TObjectPtr<class UStaticMeshComponent> Streak;
	FVector Velocity = FVector::ZeroVector;
};
