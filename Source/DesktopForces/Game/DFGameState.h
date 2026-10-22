#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "DFGameState.generated.h"

class ADFPlayerState;

/**
 * Replicated match info (MF RageGameRepInfo / RageTeamReplicationInfo).
 * Built on AGameState: MatchState (WaitingToStart / InProgress / WaitingPostMatch) and
 * ElapsedTime come from the engine; this adds the MF rules and team scores.
 */
UCLASS()
class DESKTOPFORCES_API ADFGameState : public AGameState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 0 = free-for-all; otherwise how many teams play (ids 0..NumTeams-1). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	int32 NumTeams = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	int32 FragLimit = 0;

	/** Minutes (0 = none). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	int32 TimeLimit = 0;

	/** Seconds; overrides TimeLimit when set (Safecracker part 2 must beat part 1's time). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	int32 TimeLimitSeconds = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	int32 GoalTeamScore = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	int32 RespawnWait = 1;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	TArray<int32> TeamScores;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	FString WinnerText;

	/** The vote in progress (Gregaras' vote mutator, voteHUD): what it is, yes / no so far and
	 *  when it runs out (server world time); empty: none. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	FString VoteText;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	int32 VoteYes = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	int32 VoteNo = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	float VoteEndTime = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	FString GameName;

	/**
	 * Where players may open the loadout (MF-mods CoopMode "weapons"): 0 nowhere, 1-2 only
	 * weapons lying on the ground, 3 + loadout crates, 4 + loadout zones (everything, default).
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	int32 LoadoutAccess = 4;

	/** Per team overrides of LoadoutAccess (index = team; Zombie mode limits the zombies). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	TArray<int32> TeamLoadoutAccess;

	int32 GetLoadoutAccess(uint8 Team) const
	{
		return TeamLoadoutAccess.IsValidIndex(Team) ? TeamLoadoutAccess[Team] : LoadoutAccess;
	}

	/** Inventory groups loadouts may hand out (DeBall: knife, pistol, gadgets); empty = all. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	TArray<int32> AllowedItemGroups;

	/** Kept out of loadouts, subclasses too (ADFGameMode::NoWeapons). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	TArray<TSubclassOf<class ADFWeapon>> BannedItems;

	bool IsItemAllowed(TSubclassOf<class ADFWeapon> Item) const;

	/** Parachutes for everyone (the servers' mutPack: hold Jump while falling). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	bool bParachutes = true;

	/** Shots this many times as fast; clips that never run dry (ADFWeapon::HasInfiniteAmmo).
	 *  ?OPWeapons=1 sets both (ADFGameMode::bOPWeapons). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	float FireRate = 1.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	bool bInfiniteAmmo = false;

	/** Cheats for everyone on this server (?Cheats=1, ADFGameMode::bAllowCheats). */
	UPROPERTY(ReplicatedUsing = OnRep_CheatsAllowed, BlueprintReadOnly, Category = "DF")
	bool bCheatsAllowed = false;
	UFUNCTION()
	void OnRep_CheatsAllowed();

	/** Mode-specific team names ("Human", "Zombie"); empty = UDFGameSettings::Teams. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	TArray<FString> TeamNameOverrides;

	FText GetTeamName(uint8 Team) const;

	/** ElapsedTime when the current round began (round-based modes restart the clock). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	int32 RoundStartTime = 0;

	bool IsTeamGame() const { return NumTeams > 0; }

	/** Seconds left (TimeLimit minus the time since the round began); -1 without a limit. */
	int32 GetRemainingTime() const
	{
		const int32 Limit = TimeLimitSeconds > 0 ? TimeLimitSeconds : TimeLimit * 60;
		return Limit > 0 ? FMath::Max(0, Limit - (ElapsedTime - RoundStartTime)) : -1;
	}

	int32 GetTeamScore(uint8 Team) const { return TeamScores.IsValidIndex(Team) ? TeamScores[Team] : 0; }

	TArray<ADFPlayerState*> GetSortedPlayers() const;
};
