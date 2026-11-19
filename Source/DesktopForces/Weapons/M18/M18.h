#pragma once

#include "CoreMinimal.h"
#include "Weapons/Grenades/Grenades.h"
#include "Weapons/DFProjectile.h"
#include "M18.generated.h"

/**
 * M18 smoke grenade (the community servers' weapon mods, M18.u: author still being checked, so
 * its models and icon come from the Unverified pack). Thrown like a grenade, no damage: fire's
 * pours dense smoke for 15 s (smokeGrenEffect) once its fuse runs out, alt fire's from where it
 * first lands (smokeGrenProj2: HitWall starts the smoke).
 */
UCLASS()
class DESKTOPFORCES_API AM18 : public AGrenades {
	GENERATED_BODY()
public:
	AM18();
};

/** smokeGrenProj: Grenade1's throw and bounces; the fuse starts the smoke instead of a blast. */
UCLASS()
class DESKTOPFORCES_API ASmokeGrenade : public AGrenade {
	GENERATED_BODY()
public:
	ASmokeGrenade();

protected:
	virtual void Explode(const FVector& Location, const FVector& Normal) override;
	UFUNCTION(NetMulticast, Reliable)
	void MulticastSmoke();
};

/** smokeGrenProj2 (alt fire): smokes from the first thing it hits on, still bouncing. */
UCLASS()
class DESKTOPFORCES_API ASmokeGrenadeImpact : public ASmokeGrenade {
	GENERATED_BODY()
public:
	ASmokeGrenadeImpact();
};
