#pragma once

#include "CoreMinimal.h"
#include "Core/DFTypes.h"
#include "GameFramework/Actor.h"
#include "DFBotSpot.generated.h"

class ADFWeapon;

/**
 * A RageBot the mapper placed in the map (the coop maps' defenders): it comes alive when the
 * match starts, with the mapper's Health, favourite loadout (aFavouriteLoadout) and team; with
 * GroundSpeed 0 it holds its spot and only shoots. Placed pawns don't come back once killed.
 */
UCLASS()
class DESKTOPFORCES_API ADFBotSpot : public AActor
{
	GENERATED_BODY()

public:
	ADFBotSpot();

	UPROPERTY(EditAnywhere, Category = "DF")
	int32 Health = 100;

	/** GroundSpeed 0: a sentry. */
	UPROPERTY(EditAnywhere, Category = "DF")
	bool bStationary = false;

	UPROPERTY(EditAnywhere, Category = "DF")
	TArray<TSubclassOf<ADFWeapon>> Loadout;

	UPROPERTY(EditAnywhere, Category = "DF")
	uint8 Team = 0;
};
