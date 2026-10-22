#pragma once

#include "CoreMinimal.h"
#include "Core/DFTypes.h"
#include "Weapons/TripBombs/TripBomb.h"
#include "Weapons/DFWeapons.h"
#include "TripLasers.generated.h"

/** 'TripLasersDOTTripLasers' - "%k Disintegrated %o." */
UCLASS()
class DESKTOPFORCES_API UDFDamage_TripLaser : public UDFDamageType
{
	GENERATED_BODY()
public:
	UDFDamage_TripLaser();
};

/**
 * Planted trip laser (TripLasers.TripLaser): the beam passes team-mates (anyone else's but
 * the planter's in deathmatch) and burns the first enemy to walk into it - 280 when the beam
 * ends on something, 185 when it runs out into the open (1500 uu) - then it's gone. Shooting
 * it just breaks it. Lasts 8 minutes.
 */
UCLASS()
class DESKTOPFORCES_API ATripLaser : public ATripBomb
{
	GENERATED_BODY()
public:
	ATripLaser();
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void Arm() override;
	virtual void CheckBeam() override;
	bool bAnchored = false;
	FVector BeamEnd = FVector::ZeroVector;   // full length: anything walking into it counts
};

/** Thrown trip laser lying on the ground (TripLaserOnGround): an enemy touching it takes 280. */
UCLASS()
class DESKTOPFORCES_API ATripLaserMine : public ATripMine
{
	GENERATED_BODY()
public:
	ATripLaserMine();
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
protected:
	virtual void BeginPlay() override;
	virtual bool ShouldTrigger(const ADFCharacter* Who) const override;
	virtual void Trigger(ADFCharacter* Who) override;
	uint8 PlacedTeam = DF::NoTeam;
};

/** Trip Lasers (TripLasers extends TripBombs): 3 per pick, at most 10 out per player. */
UCLASS()
class DESKTOPFORCES_API ATripLasers : public ATripBombs
{
	GENERATED_BODY()
public:
	ATripLasers();
	UPROPERTY(EditDefaultsOnly, Category = "TripLasers")
	int32 MaxActiveLasers = 10;
protected:
	virtual bool CanPlace() const override;
};
