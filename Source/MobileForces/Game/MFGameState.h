#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "MFGameState.generated.h"

class AMFPlayerState;

/**
 * Replicated match info (MF RageGameRepInfo / RageTeamReplicationInfo).
 * Built on AGameState: MatchState (WaitingToStart / InProgress / WaitingPostMatch) and
 * ElapsedTime come from the engine; this adds the MF rules and team scores.
 */
UCLASS()
class MOBILEFORCES_API AMFGameState : public AGameState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 0 = free-for-all; otherwise how many teams play (ids 0..NumTeams-1). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	int32 NumTeams = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	int32 FragLimit = 0;

	/** Minutes (0 = none). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	int32 TimeLimit = 0;

	/** Seconds; overrides TimeLimit when set (Safecracker part 2 must beat part 1's time). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	int32 TimeLimitSeconds = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	int32 GoalTeamScore = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	int32 RespawnWait = 1;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	TArray<int32> TeamScores;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	FString WinnerText;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	FString GameName;

	/** ElapsedTime when the current round began (round-based modes restart the clock). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	int32 RoundStartTime = 0;

	bool IsTeamGame() const { return NumTeams > 0; }

	/** Seconds left (TimeLimit minus the time since the round began); -1 without a limit. */
	int32 GetRemainingTime() const
	{
		const int32 Limit = TimeLimitSeconds > 0 ? TimeLimitSeconds : TimeLimit * 60;
		return Limit > 0 ? FMath::Max(0, Limit - (ElapsedTime - RoundStartTime)) : -1;
	}

	int32 GetTeamScore(uint8 Team) const { return TeamScores.IsValidIndex(Team) ? TeamScores[Team] : 0; }

	TArray<AMFPlayerState*> GetSortedPlayers() const;
};
