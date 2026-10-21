#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "NavLinkHostInterface.h"
#include "MFTeleporter.generated.h"

class UCapsuleComponent;

/**
 * Engine.Teleporter: walking into one with a URL moves you to a random other teleporter whose
 * Tag is that URL (one without a URL is only a destination). bChangesYaw turns you to face the
 * way the destination faces (keeping how you came in relative to the source), bChangesVelocity
 * sets TargetVelocity, bReverses* flip the speed. Triggering it toggles bEnabled. Placed by the
 * level builder (`mfport.py import --steps patch` adds them to built maps).
 */
UCLASS()
class DESKTOPFORCES_API AMFTeleporter : public AActor, public INavLinkHostInterface, public INavRelevantInterface
{
	GENERATED_BODY()

public:
	AMFTeleporter();

	UPROPERTY(EditAnywhere, Category = "MF")
	FString URL;

	/** The UE1 Tag other teleporters' URL points at. */
	UPROPERTY(EditAnywhere, Category = "MF")
	FName TeleporterTag;

	UPROPERTY(EditAnywhere, Category = "MF")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, Category = "MF")
	bool bChangesYaw = false;

	UPROPERTY(EditAnywhere, Category = "MF")
	bool bChangesVelocity = false;

	UPROPERTY(EditAnywhere, Category = "MF")
	FVector TargetVelocity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "MF")
	bool bReversesX = false;
	UPROPERTY(EditAnywhere, Category = "MF")
	bool bReversesY = false;
	UPROPERTY(EditAnywhere, Category = "MF")
	bool bReversesZ = false;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UCapsuleComponent> Trigger;

	/** Teleporter.Accept: Incoming arrives here from Source. */
	bool Accept(APawn* Incoming, const AMFTeleporter* Source);

	/** Where it sends you (the floor under each destination, relative to this one): one-way
	 *  navigation links, so bots path through teleporters (UE1 SpecialHandling). Level builder. */
	UPROPERTY(VisibleAnywhere, Category = "MF")
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
class DESKTOPFORCES_API AMFPeriodicSound : public AActor
{
	GENERATED_BODY()

public:
	AMFPeriodicSound();

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<class UAudioComponent> Audio;

	UPROPERTY(EditAnywhere, Category = "MF")
	float Period = 0.f;
	UPROPERTY(EditAnywhere, Category = "MF")
	float Interval = 0.f;
	UPROPERTY(EditAnywhere, Category = "MF")
	float PhaseShift = 0.f;

protected:
	virtual void BeginPlay() override;
	void Switch();
	FTimerHandle SwitchTimer;
};
