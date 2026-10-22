#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "NavLinkHostInterface.h"
#include "Teleporter.generated.h"

class UCapsuleComponent;

/**
 * Engine.Teleporter: walking into one with a URL moves you to a random other teleporter whose
 * Tag is that URL (one without a URL is only a destination). bChangesYaw turns you to face the
 * way the destination faces (keeping how you came in relative to the source), bChangesVelocity
 * sets TargetVelocity, bReverses* flip the speed. Triggering it toggles bEnabled. Placed by the
 * level builder (`mfport.py import --steps patch` adds them to built maps).
 */
UCLASS()
class DESKTOPFORCES_API ATeleporter : public AActor, public INavLinkHostInterface, public INavRelevantInterface
{
	GENERATED_BODY()

public:
	ATeleporter();

	UPROPERTY(EditAnywhere, Category = "DF")
	FString URL;

	/** The UE1 Tag other teleporters' URL points at. */
	UPROPERTY(EditAnywhere, Category = "DF")
	FName TeleporterTag;

	UPROPERTY(EditAnywhere, Category = "DF")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, Category = "DF")
	bool bChangesYaw = false;

	UPROPERTY(EditAnywhere, Category = "DF")
	bool bChangesVelocity = false;

	UPROPERTY(EditAnywhere, Category = "DF")
	FVector TargetVelocity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "DF")
	bool bReversesX = false;
	UPROPERTY(EditAnywhere, Category = "DF")
	bool bReversesY = false;
	UPROPERTY(EditAnywhere, Category = "DF")
	bool bReversesZ = false;

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<UCapsuleComponent> Trigger;

	/** Teleporter.Accept: Incoming arrives here from Source. */
	bool Accept(APawn* Incoming, const ATeleporter* Source);

	/** Where it sends you (the floor under each destination, relative to this one): one-way
	 *  navigation links, so bots path through teleporters (UE1 SpecialHandling). Level builder. */
	UPROPERTY(VisibleAnywhere, Category = "DF")
	TArray<FVector> LinkTargets;

	// INavRelevantInterface / INavLinkHostInterface (like ANavLinkProxy)
	virtual void GetNavigationData(FNavigationRelevantData& Data) const override;
	virtual FBox GetNavigationBounds() const override;
	virtual bool IsNavigationRelevant() const override { return LinkTargets.Num() > 0; }
	virtual bool GetNavigationLinksClasses(TArray<TSubclassOf<UNavLinkDefinition>>& OutClasses) const override { return false; }
	virtual bool GetNavigationLinksArray(TArray<FNavigationLink>& OutLink, TArray<FNavigationSegmentLink>& OutSegments) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnTouch(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);
	UFUNCTION()
	void OnUntouch(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex);

	/** Just arrived here: not sent on until they step out (UE1 Accept: Disable('Touch')). */
	TSet<TWeakObjectPtr<AActor>> Arrived;
	TArray<FNavigationLink> MakeLinks() const;
};

/**
 * Engine.PeriodicSound: an ambient loop that plays for Period seconds, then pauses Interval
 * seconds, PhaseShift seconds out of step with the others (Period 0: always on).
 */
UCLASS()
class DESKTOPFORCES_API ADFPeriodicSound : public AActor
{
	GENERATED_BODY()

public:
	ADFPeriodicSound();

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<class UAudioComponent> Audio;

	UPROPERTY(EditAnywhere, Category = "DF")
	float Period = 0.f;
	UPROPERTY(EditAnywhere, Category = "DF")
	float Interval = 0.f;
	UPROPERTY(EditAnywhere, Category = "DF")
	float PhaseShift = 0.f;

protected:
	virtual void BeginPlay() override;
	void Switch();
	FTimerHandle SwitchTimer;
};
