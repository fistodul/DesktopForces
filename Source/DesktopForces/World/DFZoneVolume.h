#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PhysicsVolume.h"
#include "Core/DFTypes.h"
#include "DFZoneVolume.generated.h"

/** 'Burned', 'Corroded' ... : MF pain zones (lava, acid, death water, space). */
UCLASS()
class DESKTOPFORCES_API UDFDamage_Zone : public UDFDamageType
{
	GENERATED_BODY()
public:
	UDFDamage_Zone() { bBypassArmour = true; }
};

/**
 * A UE1 zone with gameplay: water (RageWaterZone / bWaterZone: UE swimming), a pain zone
 * (bPainZone + DamagePerSec; negative heals, e.g. mf-City_Rush), a current (ZoneVelocity) and
 * its own gravity (bGravityZone).
 * Placed by the level builder over the zone's bounds.
 */
UCLASS()
class DESKTOPFORCES_API ADFZoneVolume : public APhysicsVolume
{
	GENERATED_BODY()

public:
	ADFZoneVolume();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void ActorEnteredVolume(AActor* Other) override;

	/** MF DamagePerSec (per second; Pawn.PainTimer runs once a second). Negative heals. */
	UPROPERTY(EditAnywhere, Category = "DF")
	float DamagePerSec = 0.f;

	/** MF ZoneVelocity, cm/s: pushes soldiers in the zone along. */
	UPROPERTY(EditAnywhere, Category = "DF")
	FVector ZoneVelocity = FVector::ZeroVector;

	/** MF bGravityZone: ZoneGravity instead of the world's. Soldiers feel it; vehicles keep
	 *  the world's. */
	UPROPERTY(EditAnywhere, Category = "DF")
	bool bGravityZone = false;

	/** cm/s2. mf-warehouse's roof vents: (0, 0, -225), an updraft (the world pulls -2138);
	 *  sideways parts push soldiers along (mf-Prehistoric). */
	UPROPERTY(EditAnywhere, Category = "DF", meta = (EditCondition = "bGravityZone"))
	FVector ZoneGravity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "DF")
	FString ZoneName;

	virtual float GetGravityZ() const override;

	/** Water surface height (top of the volume). */
	float GetSurfaceZ() const;
	/** The water volume a point is in (none: nullptr). */
	static ADFZoneVolume* FindWater(const UWorld* World, const FVector& Point);

protected:
	void PainTimer();
	FTimerHandle PainHandle;
};
