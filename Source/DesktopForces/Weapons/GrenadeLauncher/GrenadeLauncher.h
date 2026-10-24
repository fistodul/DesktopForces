#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapon.h"
#include "Weapons/DFProjectile.h"
#include "GrenadeLauncher.generated.h"

/** grenLaunch launcherGrenade1: impact grenade, 250 damage in 200 uu. */
UCLASS()
class DESKTOPFORCES_API ALauncherGrenade : public AImpactGrenade {
	GENERATED_BODY()
public:
	ALauncherGrenade();
};

/** grenFragment: one of the cluster's 15 bomblets, 20 damage (300 / 15) in 200 uu. */
UCLASS()
class DESKTOPFORCES_API AGrenadeFragment : public AImpactGrenade {
	GENERATED_BODY()
public:
	AGrenadeFragment();
};

/** grenLaunch launcherGrenade (alt fire): bursts into 15 fragments where it lands. */
UCLASS()
class DESKTOPFORCES_API AClusterGrenade : public AImpactGrenade {
	GENERATED_BODY()
public:
	AClusterGrenade();
	virtual void Explode(const FVector& Location, const FVector& Normal) override;

	UPROPERTY(EditDefaultsOnly, Category = "DF")
	int32 FragmentCount = 15; // altFireFragmentCount
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float FragmentSpread = 1.f; // altFireFragmentSpread
};

/**
 * Grenade Launcher (MF-mods grenLaunch): one round a load, 6 loads. Fire: an impact grenade
 * (250, 200 uu); alt: a cluster grenade that bursts into 15 fragments.
 */
UCLASS()
class DESKTOPFORCES_API AGrenadeLauncher : public ADFWeapon {
	GENERATED_BODY()
public:
	AGrenadeLauncher();
};
