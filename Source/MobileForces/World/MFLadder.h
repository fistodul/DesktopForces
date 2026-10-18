#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MFLadder.generated.h"

class UCapsuleComponent;

/**
 * Ladder climbing volume (MF LadderClimber: a hidden cylinder in front of the ladder,
 * rotated to face the wall). Touching it switches the player to ladder movement
 * (UMFCharacterMovement, MF PHYS_Ladder); leaving it drops back to walking/falling.
 * Event driven (overlaps), never ticks.
 */
UCLASS()
class MOBILEFORCES_API AMFLadderVolume : public AActor
{
	GENERATED_BODY()

public:
	AMFLadderVolume();
	virtual void OnConstruction(const FTransform& Transform) override;

	/** MF LadderClimber.ClimbSpeed (0.5 = normal speed). */
	UPROPERTY(EditAnywhere, Category = "MF")
	float ClimbSpeed = 0.5f;

	UPROPERTY(EditAnywhere, Category = "MF")
	float Radius = 72.f;          // MF CollisionRadius 32

	UPROPERTY(EditAnywhere, Category = "MF")
	float HalfHeight = 342.f;     // MF CollisionHeight 152

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UCapsuleComponent> Volume;

	UFUNCTION()
	void OnBegin(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body, bool bSweep, const FHitResult& Hit);
	UFUNCTION()
	void OnEnd(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body);
};
