#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapons.h"
#include "Weapons/DFProjectile.h"
#include "Molotov.generated.h"

/** 'MolotovDOTMolotov' - "%k set %o on fire." */
UCLASS()
class DESKTOPFORCES_API UDFDamage_Fire : public UDFDamageType {
	GENERATED_BODY()
public:
	UDFDamage_Fire();
};

/**
 * Burning patch left by a Molotov (Molotov.MolotovFire): ~7 s, 26 damage/s to everything
 * within 300 uu (half that vertically) that it can see, or that is very close (fire
 * spreading through doors). Timer driven, never ticks.
 */
UCLASS()
class DESKTOPFORCES_API AMolotovFire : public AActor {
	GENERATED_BODY()
public:
	AMolotovFire();

protected:
	virtual void BeginPlay() override;
	void Burn();

	UPROPERTY(EditDefaultsOnly, Category = "Molotov")
	float BurnDuration = 6.9f; // + rand(0..1)
	UPROPERTY(EditDefaultsOnly, Category = "Molotov")
	float DamagePerTick = 2.6f; // every 0.1 s
	UPROPERTY(EditDefaultsOnly, Category = "Molotov")
	float FireRadius = 675.f; // MF 300 uu

	float Accumulator = 0.f;
	FTimerHandle BurnTimer;
};

/** Timed Molotov (MolotovProjectile: Grenade1 with 44 damage). */
UCLASS()
class DESKTOPFORCES_API AMolotovBottle : public AGrenade {
	GENERATED_BODY()
public:
	AMolotovBottle();

protected:
	virtual void Explode(const FVector& Location, const FVector& Normal) override;
};

/** Impact Molotov (MolotovProjectileAlt: Grenade2 with 40 damage). */
UCLASS()
class DESKTOPFORCES_API AMolotovImpact : public AImpactGrenade {
	GENERATED_BODY()
public:
	AMolotovImpact();

protected:
	virtual void Explode(const FVector& Location, const FVector& Normal) override;
};

/** The weapon: grenades with 2 Molotovs per pick (Molotov extends Grenades). */
UCLASS()
class DESKTOPFORCES_API AMolotov : public AGrenades {
	GENERATED_BODY()
public:
	AMolotov();
};
