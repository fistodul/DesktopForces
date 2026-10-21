#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PhysicsVolume.h"
#include "Core/MFTypes.h"
#include "MFZoneVolume.generated.h"

/** 'Burned', 'Corroded' ... : MF pain zones (lava, acid, death water, space). */
UCLASS()
class DESKTOPFORCES_API UMFDamage_Zone : public UMFDamageType
{
	GENERATED_BODY()
public:
	UMFDamage_Zone() { bBypassArmour = true; }
};

/**
 * A UE1 zone with gameplay: water (RageWaterZone / bWaterZone: UE swimming), a pain zone
 * (bPainZone + DamagePerSec; negative heals, e.g. mf-City_Rush) and a current (ZoneVelocity).
 * Placed by the level builder over the zone's bounds.
 */
UCLASS()
class DESKTOPFORCES_API AMFZoneVolume : public APhysicsVolume
{
	GENERATED_BODY()

public:
	AMFZoneVolume();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void ActorEnteredVolume(AActor* Other) override;

	/** MF DamagePerSec (per second; Pawn.PainTimer runs once a second). Negative heals. */
	UPROPERTY(EditAnywhere, Category = "MF")
	float DamagePerSec = 0.f;

	/** MF ZoneVelocity, cm/s: pushes soldiers in the zone along. */
	UPROPERTY(EditAnywhere, Category = "MF")
	FVector ZoneVelocity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "MF")
	FString ZoneName;

	/** Water surface height (top of the volume). */
	float GetSurfaceZ() const;
	/** The water volume a point is in (none: nullptr). */
	static AMFZoneVolume* FindWater(const UWorld* World, const FVector& Point);

protected:
	void PainTimer();
	FTimerHandle PainHandle;
};
