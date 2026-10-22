#pragma once

#include "CoreMinimal.h"
#include "Weapons/RocketLauncher/RocketLauncher.h"
#include "Weapons/DFProjectile.h"
#include "ImprovedRL.generated.h"

/**
 * Gian's improved rocket launcher (improvedRL.u): the stock launcher made lighter (carry size 3
 * instead of 4) and its rockets faster and a little harder hitting - an assault launcher. The
 * mod's lock-on rocket never steered (its seeking state was renamed but still entered by the old
 * name); here the lock-on works as on the stock launcher.
 */
UCLASS()
class DESKTOPFORCES_API AImprovedRL : public ARocketLauncher
{
	GENERATED_BODY()
public:
	AImprovedRL();
};

/** improvedRL.ImprovedRocket: 1800 uu/s (stock 1200), 140 damage (stock 135). */
UCLASS()
class DESKTOPFORCES_API AImprovedRocket : public ARocket
{
	GENERATED_BODY()
public:
	AImprovedRocket();
};
