#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/DFTypes.h"
#include "DFLoadout.generated.h"

class ADFWeapon;
class ADFCharacter;
class UBoxComponent;
class UCapsuleComponent;
class UStaticMeshComponent;

/**
 * Something players can open the loadout screen at (MF: LoadoutCrate / LoadoutZone).
 * Holds the list of items on offer (MF aWeaponsInStore) and who may use it.
 */
UCLASS(Abstract)
class DESKTOPFORCES_API ADFLoadoutSource : public AActor {
	GENERATED_BODY()

public:
	ADFLoadoutSource();

	/** Items on offer, in menu order. Empty = the game-wide list (UDFGameSettings::LoadoutItems). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DF")
	TArray<TSubclassOf<ADFWeapon>> Items;

	const TArray<TSubclassOf<ADFWeapon>>& GetItems() const;

	/** Owning team id; 255 = everyone (deathmatch crates). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DF")
	uint8 Team = DF::NoTeam;

	/** Exists in team games (MF Actor.bTeamPlay). */
	UPROPERTY(EditAnywhere, Category = "DF")
	bool bTeamPlay = true;

	/** Exists in non-team games (MF Actor.bNonTeamPlay). */
	UPROPERTY(EditAnywhere, Category = "DF")
	bool bNonTeamPlay = true;

	virtual bool IsUsableBy(const ADFCharacter* Character) const;
	/** ADFGameState::LoadoutAccess this kind of source needs (crate 3, zone 4). */
	virtual int32 GetRequiredAccess() const { return 3; }
	virtual bool IsInside(const ADFCharacter* Character) const;

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
class DESKTOPFORCES_API ALoadoutCrate : public ADFLoadoutSource {
	GENERATED_BODY()

public:
	ALoadoutCrate();
	virtual bool IsInside(const ADFCharacter* Character) const override;

	/** Visual style from the MF class name (Open / Skewed / Closed), or Hidden (trigger only). */
	UPROPERTY(EditAnywhere, Category = "DF")
	FName Variant;

	bool IsVisibleCrate() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<UCapsuleComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<UStaticMeshComponent> Mesh;

	void UpdateLook();
};

/** Loadout zone (MF LoadoutZone: the team spawn room). Represented by a box around the zone. */
UCLASS()
class DESKTOPFORCES_API ALoadoutZone : public ADFLoadoutSource {
	GENERATED_BODY()

public:
	virtual int32 GetRequiredAccess() const override { return 4; }
	ALoadoutZone();
	virtual bool IsInside(const ADFCharacter* Character) const override;

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<UBoxComponent> Volume;
};

UCLASS()
class DESKTOPFORCES_API UDFLoadoutLibrary : public UBlueprintFunctionLibrary {
	GENERATED_BODY()

public:
	/** The crate or zone the character can open the loadout at, or null. */
	UFUNCTION(BlueprintPure, Category = "DF")
	static ADFLoadoutSource* FindLoadoutSourceFor(const ADFCharacter* Character);

	/** The game-wide item list (UDFGameSettings::LoadoutItems, mods can extend it). */
	static const TArray<TSubclassOf<ADFWeapon>>& DefaultItems();
};
