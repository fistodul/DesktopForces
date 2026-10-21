#pragma once

#include "CoreMinimal.h"
#include "Weapons/MFWeapons.h"
#include "Weapons/MFProjectile.h"
#include "MFMolotov.generated.h"

/** 'MolotovDOTMolotov' - "%k set %o on fire." */
UCLASS()
class DESKTOPFORCES_API UMFDamage_Fire : public UMFDamageType
{
	GENERATED_BODY()
public:
	UMFDamage_Fire();
};

/**
 * Burning patch left by a Molotov (Molotov.MolotovFire): ~7 s, 26 damage/s to everything
 * within 300 uu (half that vertically) that it can see, or that is very close (fire
 * spreading through doors). Timer driven, never ticks.
 */
UCLASS()
class DESKTOPFORCES_API AMFMolotovFire : public AActor
{
	GENERATED_BODY()
public:
	AMFMolotovFire();
protected:
	virtual void BeginPlay() override;
	void Burn();

	UPROPERTY(EditDefaultsOnly, Category = "Molotov")
	float BurnDuration = 6.9f;        // + rand(0..1)
	UPROPERTY(EditDefaultsOnly, Category = "Molotov")
	float DamagePerTick = 2.6f;       // every 0.1 s
	UPROPERTY(EditDefaultsOnly, Category = "Molotov")
	float FireRadius = 675.f;         // MF 300 uu

	float Accumulator = 0.f;
	FTimerHandle BurnTimer;
};

/** Timed Molotov (MolotovProjectile: Grenade1 with 44 damage). */
UCLASS()
class DESKTOPFORCES_API AMFProjectile_Molotov : public AMFProjectile_Grenade
{
	GENERATED_BODY()
public:
	AMFProjectile_Molotov();
protected:
	virtual void Explode(const FVector& Location, const FVector& Normal) override;
};

/** Impact Molotov (MolotovProjectileAlt: Grenade2 with 40 damage). */
UCLASS()
class DESKTOPFORCES_API AMFProjectile_MolotovImpact : public AMFProjectile_ImpactGrenade
{
	GENERATED_BODY()
public:
	AMFProjectile_MolotovImpact();
protected:
	virtual void Explode(const FVector& Location, const FVector& Normal) override;
};

/** The weapon: grenades with 2 Molotovs per pick (Molotov extends Grenades). */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_Molotov : public AMFWeapon_Grenades
{
	GENERATED_BODY()
public:
	AMFWeapon_Molotov();
};
