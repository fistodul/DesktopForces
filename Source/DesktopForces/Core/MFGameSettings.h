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

/** An extra vehicle on a map (mutPack addVehicles: its per-map spots, 16 a map). */
USTRUCT(BlueprintType)
struct FMFAddedVehicle
{
	GENERATED_BODY()

	/** Map file name, any case ("mf-treasure_finders"). */
	UPROPERTY(Config, EditAnywhere, Category = "Vehicles")
	FString Map;

	/** MF class: "heli.heli", "piper.piperTs", "jeep.jeep", "boat.boat" ... */
	UPROPERTY(Config, EditAnywhere, Category = "Vehicles")
	FString VehicleClass;

	UPROPERTY(Config, EditAnywhere, Category = "Vehicles")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(Config, EditAnywhere, Category = "Vehicles")
	float Yaw = 0.f;
};

/** One map's vehicle generators of one kind make another vehicle (BR-thebigisland's trucks
 *  were placed for mutPack replaceVehicles to turn into helis). */
USTRUCT()
struct FMFVehicleSwap
{
	GENERATED_BODY()

	/** Map file name, any case. */
	UPROPERTY(Config, EditAnywhere, Category = "Vehicles")
	FString Map;

	/** The generator's MF vehicle: "Rage.Truck", "Rage.Buggy" ... */
	UPROPERTY(Config, EditAnywhere, Category = "Vehicles")
	FString From;

	/** What it makes instead: "heli.heli", "jeep.jeep" ... */
	UPROPERTY(Config, EditAnywhere, Category = "Vehicles")
	FString To;
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
 * Game-wide data in Config/DefaultGame.ini ([/Script/DesktopForces.MFGameSettings]), also
 * shown under Project Settings > Game > Mobile Forces.
 *
 * Text config instead of hard-coded lists so content can grow without code changes.
 * Plugin config files are not merged into the project's Game config, so mod weapons
 * register themselves (AMFWeapon::bAddToLoadout); listing one here instead fixes its order:
 *   +LoadoutItems=/Script/DesktopForces.MFWeapon_Molotov
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Mobile Forces"))
class DESKTOPFORCES_API UMFGameSettings : public UDeveloperSettings
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

	/** Extra vehicles per map (the community servers' mutPack addVehicles; ?AddVehicles=0 off). */
	UPROPERTY(Config, EditAnywhere, Category = "Vehicles")
	TArray<FMFAddedVehicle> AddedVehicles;

	/** Per-map generator swaps (+VehicleSwaps=(Map=...,From=...,To=...)). */
	UPROPERTY(Config, EditAnywhere, Category = "Vehicles")
	TArray<FMFVehicleSwap> VehicleSwaps;

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

	/** Title screen pictures, one picked at random per start (scaled to fill the screen). */
	UPROPERTY(Config, EditAnywhere, Category = "Menu")
	TArray<TSoftObjectPtr<class UTexture2D>> MenuBackgrounds;

	/** Played once when the title screen first shows. */
	UPROPERTY(Config, EditAnywhere, Category = "Menu")
	TSoftObjectPtr<class USoundBase> MenuMusic;

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
