#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DFPickupSpot.generated.h"

class ADFWeapon;

/**
 * Where the map placed an item (a weapon, grenades, armour ... lying around): it lies there
 * for anyone to pick up and comes back RespawnTime seconds after it was taken
 * (RageDeathMatch.ShouldRespawn; Inventory.RespawnTime 30 s, 0 = once). Mappers' MaxClipAmmo /
 * MaxClips carry over. Modes that limit the items (ADFGameState::IsItemAllowed) leave it empty.
 */
UCLASS()
class DESKTOPFORCES_API ADFPickupSpot : public AActor {
	GENERATED_BODY()
	friend class FDFMapPickupTest;

public:
	ADFPickupSpot();

	UPROPERTY(EditAnywhere, Category = "DF")
	TSubclassOf<ADFWeapon> ItemClass;

	UPROPERTY(EditAnywhere, Category = "DF")
	float RespawnTime = 30.f;

	/** One-shot authored pickups may collect on player touch; normal drops stay manual. */
	UPROPERTY(EditAnywhere, Category = "DF")
	bool bAutoPickup = false;
	UPROPERTY(EditAnywhere, Category = "DF")
	float TouchRadius = 50.f;
	UPROPERTY(EditAnywhere, Category = "DF")
	float TouchHalfHeight = 50.f;

	/** 0: the item's own. */
	UPROPERTY(EditAnywhere, Category = "DF")
	int32 MaxClipAmmo = 0;
	UPROPERTY(EditAnywhere, Category = "DF")
	int32 MaxClips = 0;

protected:
	virtual void BeginPlay() override;
	void SpawnItem();
	void Check();
	void CheckTouching();
	void TryPickup(class ADFCharacter* Soldier);
	UFUNCTION()
	void OnTouch(class UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent, int32 BodyIndex, bool bFromSweep, const FHitResult& Hit);
	UPROPERTY()
	TObjectPtr<class UCapsuleComponent> PickupTouch;

	TWeakObjectPtr<ADFWeapon> Item;
	FTimerHandle CheckTimer;
	FTimerHandle RespawnTimer;
};
