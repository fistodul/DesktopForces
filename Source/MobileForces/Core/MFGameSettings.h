#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MFGameSettings.generated.h"

class AMFWeapon;
class UMaterialInterface;

/** A team (any number can be defined; game modes use the first NumTeams). */
USTRUCT(BlueprintType)
struct FMFTeamDef
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Team")
	FText Name;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Team")
	FLinearColor Color = FLinearColor::White;
};

/** A named bot: who it is and what it takes from the loadout (MF RageBotA-P, BonusBotA-P). */
USTRUCT(BlueprintType)
struct FMFBotDef
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bot")
	FString Name;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bot")
	TArray<TSoftClassPtr<AMFWeapon>> Loadout;

	/** Team the bot likes to join in team games (255 = any). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bot")
	uint8 PreferredTeam = 255;
};

/**
 * Game-wide data in Config/DefaultGame.ini ([/Script/MobileForces.MFGameSettings]), also
 * shown under Project Settings > Game > Mobile Forces.
 *
 * Text config instead of hard-coded lists so content can grow without code changes.
 * Plugin config files are not merged into the project's Game config, so mod weapons
 * register themselves (AMFWeapon::bAddToLoadout); listing one here instead fixes its order:
 *   +LoadoutItems=/Script/MFModMolotov.MFWeapon_Molotov
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Mobile Forces"))
class MOBILEFORCES_API UMFGameSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMFGameSettings();

	static const UMFGameSettings& Get() { return *GetDefault<UMFGameSettings>(); }

	/** Team table; team id = index (MF: 0 blue, 1 red; 255 = no team). */
	UPROPERTY(Config, EditAnywhere, Category = "Teams")
	TArray<FMFTeamDef> Teams;

	/** What loadout crates/zones offer unless a map overrides it (MF LoadoutCrate.aWeaponsInStore). */
	UPROPERTY(Config, EditAnywhere, Category = "Loadout")
	TArray<TSoftClassPtr<AMFWeapon>> LoadoutItems;

	/** Bot roster, used in order (MF: 32 bots). Add as many as you like. */
	UPROPERTY(Config, EditAnywhere, Category = "Bots")
	TArray<FMFBotDef> Bots;

	/** Unlit material coloured by custom primitive data 0-3, for team-coloured placeholders. */
	UPROPERTY(Config, EditAnywhere, Category = "Look")
	TSoftObjectPtr<UMaterialInterface> TeamTintMaterial;

	UMaterialInterface* GetTeamTintMaterial() const { return TeamTintMaterial.LoadSynchronous(); }

	/** Loadout crate look (lit, team colour from custom primitive data 0-2). */
	UPROPERTY(Config, EditAnywhere, Category = "Look")
	TSoftObjectPtr<UMaterialInterface> CrateMaterial;

	/** Global material parameters (MPC_MF: Ambient). */
	UPROPERTY(Config, EditAnywhere, Category = "Look")
	TSoftObjectPtr<class UMaterialParameterCollection> MaterialParameters;

	/** Title screen backdrop, used when it exists (generated maps are local); else the default map. */
	UPROPERTY(Config, EditAnywhere, Category = "Menu")
	FString MenuBackdropMap;

	/** Unshadowed ambient floor = map ambient (MF zone ambient) x this; the rest of it is baked. */
	UPROPERTY(Config, EditAnywhere, Category = "Look")
	float AmbientFloor = 0.35f;

	FText GetTeamName(uint8 TeamId) const;
	FLinearColor GetTeamColor(uint8 TeamId) const;

	/** Loaded loadout item classes (synchronous load, cached after first use). */
	const TArray<TSubclassOf<AMFWeapon>>& GetLoadoutItems() const;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

private:
	mutable TArray<TSubclassOf<AMFWeapon>> CachedItems;
};
