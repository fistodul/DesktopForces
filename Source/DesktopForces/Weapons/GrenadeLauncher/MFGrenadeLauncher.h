#pragma once

#include "CoreMinimal.h"
#include "Weapons/MFWeapon.h"
#include "Weapons/MFProjectile.h"
#include "MFGrenadeLauncher.generated.h"

/** grenLaunch launcherGrenade1: impact grenade, 250 damage in 200 uu. */
UCLASS()
class DESKTOPFORCES_API AMFProjectile_LauncherGrenade : public AMFProjectile_ImpactGrenade
{
	GENERATED_BODY()
public:
	AMFProjectile_LauncherGrenade();
};

/** grenFragment: one of the cluster's 15 bomblets, 20 damage (300 / 15) in 200 uu. */
UCLASS()
class DESKTOPFORCES_API AMFProjectile_GrenadeFragment : public AMFProjectile_ImpactGrenade
{
	GENERATED_BODY()
public:
	AMFProjectile_GrenadeFragment();
};

/** grenLaunch launcherGrenade (alt fire): bursts into 15 fragments where it lands. */
UCLASS()
class DESKTOPFORCES_API AMFProjectile_ClusterGrenade : public AMFProjectile_ImpactGrenade
{
	GENERATED_BODY()
public:
	AMFProjectile_ClusterGrenade();
	virtual void Explode(const FVector& Location, const FVector& Normal) override;

	UPROPERTY(EditDefaultsOnly, Category = "MF")
	int32 FragmentCount = 15;           // altFireFragmentCount
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float FragmentSpread = 1.f;         // altFireFragmentSpread
};

/**
 * Grenade Launcher (MF-mods grenLaunch): one round a load, 6 loads. Fire: an impact grenade
 * (250, 200 uu); alt: a cluster grenade that bursts into 15 fragments.
 */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_GrenadeLauncher : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_GrenadeLauncher();
};
