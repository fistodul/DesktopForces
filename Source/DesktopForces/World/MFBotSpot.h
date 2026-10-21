#pragma once

#include "CoreMinimal.h"
#include "Core/MFTypes.h"
#include "GameFramework/Actor.h"
#include "MFBotSpot.generated.h"

class AMFWeapon;

/**
 * A RageBot the mapper placed in the map (the coop maps' defenders): it comes alive when the
 * match starts, with the mapper's Health, favourite loadout (aFavouriteLoadout) and team; with
 * GroundSpeed 0 it holds its spot and only shoots. Placed pawns don't come back once killed.
 */
UCLASS()
class DESKTOPFORCES_API AMFBotSpot : public AActor
{
	GENERATED_BODY()

public:
	AMFBotSpot();

	UPROPERTY(EditAnywhere, Category = "MF")
	int32 Health = 100;

	/** GroundSpeed 0: a sentry. */
	UPROPERTY(EditAnywhere, Category = "MF")
	bool bStationary = false;

	UPROPERTY(EditAnywhere, Category = "MF")
	TArray<TSubclassOf<AMFWeapon>> Loadout;

	UPROPERTY(EditAnywhere, Category = "MF")
	uint8 Team = 0;
};
