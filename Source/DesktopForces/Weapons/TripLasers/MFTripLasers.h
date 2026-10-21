#pragma once

#include "CoreMinimal.h"
#include "Core/MFTypes.h"
#include "Weapons/TripBombs/MFTripBomb.h"
#include "Weapons/MFWeapons.h"
#include "MFTripLasers.generated.h"

/** 'TripLasersDOTTripLasers' - "%k Disintegrated %o." */
UCLASS()
class DESKTOPFORCES_API UMFDamage_TripLaser : public UMFDamageType
{
	GENERATED_BODY()
public:
	UMFDamage_TripLaser();
};

/**
 * Planted trip laser (TripLasers.TripLaser): the beam passes team-mates (anyone else's but
 * the planter's in deathmatch) and burns the first enemy to walk into it - 280 when the beam
 * ends on something, 185 when it runs out into the open (1500 uu) - then it's gone. Shooting
 * it just breaks it. Lasts 8 minutes.
 */
UCLASS()
class DESKTOPFORCES_API AMFTripLaser : public AMFTripBomb
{
	GENERATED_BODY()
public:
	AMFTripLaser();
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void Arm() override;
	virtual void CheckBeam() override;
	bool bAnchored = false;
	FVector BeamEnd = FVector::ZeroVector;   // full length: anything walking into it counts
};

/** Thrown trip laser lying on the ground (TripLaserOnGround): an enemy touching it takes 280. */
UCLASS()
class DESKTOPFORCES_API AMFTripLaserMine : public AMFProjectile_TripMine
{
	GENERATED_BODY()
public:
	AMFTripLaserMine();
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
protected:
	virtual void BeginPlay() override;
	virtual bool ShouldTrigger(const AMFCharacter* Who) const override;
	virtual void Trigger(AMFCharacter* Who) override;
	uint8 PlacedTeam = MF::NoTeam;
};

/** Trip Lasers (TripLasers extends TripBombs): 3 per pick, at most 10 out per player. */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_TripLasers : public AMFWeapon_TripBombs
{
	GENERATED_BODY()
public:
	AMFWeapon_TripLasers();
	UPROPERTY(EditDefaultsOnly, Category = "TripLasers")
	int32 MaxActiveLasers = 10;
protected:
	virtual bool CanPlace() const override;
};
