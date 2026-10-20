#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "NavLinkHostInterface.h"
#include "MFLadder.generated.h"

class UCapsuleComponent;

/**
 * Ladder climbing volume (MF LadderClimber: a hidden cylinder in front of the ladder,
 * rotated to face the wall). Touching it lets the player grab it (UMFCharacterMovement,
 * MF PHYS_Ladder): falling into it, or walking at the rungs once the centre is inside;
 * leaving it drops back to walking/falling.
 * Event driven (overlaps), never ticks.
 *
 * Also a two-way navigation link (like ANavLinkProxy) from the foot of the ladder to the
 * ledge it leads to, so bots path over it; the level builder places the ends by tracing.
 */
UCLASS()
class DESKTOPFORCES_API AMFLadderVolume : public AActor, public INavLinkHostInterface, public INavRelevantInterface
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

	/** Nav link ends, relative to the actor (bottom in front of the ladder, top on the ledge). */
	UPROPERTY(EditAnywhere, Category = "MF|Navigation", meta = (MakeEditWidget))
	FVector LinkBottom = FVector(-110.f, 0.f, -342.f);
	UPROPERTY(EditAnywhere, Category = "MF|Navigation", meta = (MakeEditWidget))
	FVector LinkTop = FVector(140.f, 0.f, 342.f);

#if WITH_EDITOR
	/** Trace the floor in front of the ladder and the ledge behind its top (level builder). */
	void PlaceNavLinks();
#endif

	// INavRelevantInterface
	virtual void GetNavigationData(FNavigationRelevantData& Data) const override;
	virtual FBox GetNavigationBounds() const override;
	virtual bool IsNavigationRelevant() const override { return true; }
	// INavLinkHostInterface
	virtual bool GetNavigationLinksClasses(TArray<TSubclassOf<UNavLinkDefinition>>& OutClasses) const override { return false; }
	virtual bool GetNavigationLinksArray(TArray<FNavigationLink>& OutLink, TArray<FNavigationSegmentLink>& OutSegments) const override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UCapsuleComponent> Volume;

	UFUNCTION()
	void OnBegin(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body, bool bSweep, const FHitResult& Hit);
	UFUNCTION()
	void OnEnd(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body);

	FNavigationLink MakeLink() const;
};
