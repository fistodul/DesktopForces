#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MFPickupSpot.generated.h"

class AMFWeapon;

/**
 * Where the map placed an item (a weapon, grenades, armour ... lying around): it lies there
 * for anyone to pick up and comes back RespawnTime seconds after it was taken
 * (RageDeathMatch.ShouldRespawn; Inventory.RespawnTime 30 s, 0 = once). Mappers' MaxClipAmmo /
 * MaxClips carry over. Modes that limit the items (AMFGameState::IsItemAllowed) leave it empty.
 */
UCLASS()
class DESKTOPFORCES_API AMFPickupSpot : public AActor
{
	GENERATED_BODY()

public:
	AMFPickupSpot();

	UPROPERTY(EditAnywhere, Category = "MF")
	TSubclassOf<AMFWeapon> ItemClass;

	UPROPERTY(EditAnywhere, Category = "MF")
	float RespawnTime = 30.f;

	/** 0: the item's own. */
	UPROPERTY(EditAnywhere, Category = "MF")
	int32 MaxClipAmmo = 0;
	UPROPERTY(EditAnywhere, Category = "MF")
	int32 MaxClips = 0;

protected:
	virtual void BeginPlay() override;
	void SpawnItem();
	void Check();

	TWeakObjectPtr<AMFWeapon> Item;
	FTimerHandle CheckTimer;
	FTimerHandle RespawnTimer;
};
