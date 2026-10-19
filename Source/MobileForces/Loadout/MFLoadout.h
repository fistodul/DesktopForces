#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/MFTypes.h"
#include "MFLoadout.generated.h"

class AMFWeapon;
class AMFCharacter;
class UBoxComponent;
class UCapsuleComponent;
class UStaticMeshComponent;

/**
 * Something players can open the loadout screen at (MF: LoadoutCrate / LoadoutZone).
 * Holds the list of items on offer (MF aWeaponsInStore) and who may use it.
 */
UCLASS(Abstract)
class MOBILEFORCES_API AMFLoadoutSource : public AActor
{
	GENERATED_BODY()

public:
	AMFLoadoutSource();

	/** Items on offer, in menu order. Empty = the game-wide list (UMFGameSettings::LoadoutItems). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MF")
	TArray<TSubclassOf<AMFWeapon>> Items;

	const TArray<TSubclassOf<AMFWeapon>>& GetItems() const;

	/** Owning team id; 255 = everyone (deathmatch crates). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MF")
	uint8 Team = MF::NoTeam;

	/** Exists in team games (MF Actor.bTeamPlay). */
	UPROPERTY(EditAnywhere, Category = "MF")
	bool bTeamPlay = true;

	/** Exists in non-team games (MF Actor.bNonTeamPlay). */
	UPROPERTY(EditAnywhere, Category = "MF")
	bool bNonTeamPlay = true;

	virtual bool IsUsableBy(const AMFCharacter* Character) const;
	virtual bool IsInside(const AMFCharacter* Character) const;

	/** Relevant in the current game type (works on clients through the game state). */
	bool IsRelevantNow() const;

protected:
	virtual void BeginPlay() override;
};

/**
 * Loadout crate (MF LoadoutCrate_Blue/Red/DM [Open|Skewed] decoration + its LoadoutCrate
 * trigger: radius 129, height 26 uu). Hidden crates on team PlayerStarts give loadout at
 * spawn on maps without walled loadout rooms (map-content 10G).
 */
UCLASS()
class MOBILEFORCES_API AMFLoadoutCrate : public AMFLoadoutSource
{
	GENERATED_BODY()

public:
	AMFLoadoutCrate();
	virtual bool IsInside(const AMFCharacter* Character) const override;

	/** Visual style from the MF class name (Open / Skewed / Closed), or Hidden (trigger only). */
	UPROPERTY(EditAnywhere, Category = "MF")
	FName Variant;

	bool IsVisibleCrate() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UCapsuleComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UStaticMeshComponent> Mesh;

	void UpdateLook();
};

/** Loadout zone (MF LoadoutZone: the team spawn room). Represented by a box around the zone. */
UCLASS()
class MOBILEFORCES_API AMFLoadoutZone : public AMFLoadoutSource
{
	GENERATED_BODY()

public:
	AMFLoadoutZone();
	virtual bool IsInside(const AMFCharacter* Character) const override;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UBoxComponent> Volume;
};

UCLASS()
class MOBILEFORCES_API UMFLoadoutLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The crate or zone the character can open the loadout at, or null. */
	UFUNCTION(BlueprintPure, Category = "MF")
	static AMFLoadoutSource* FindLoadoutSourceFor(const AMFCharacter* Character);

	/** The game-wide item list (UMFGameSettings::LoadoutItems, mods can extend it). */
	static const TArray<TSubclassOf<AMFWeapon>>& DefaultItems();
};
