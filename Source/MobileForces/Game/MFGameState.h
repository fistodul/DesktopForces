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

	bool IsTeamGame() const { return NumTeams > 0; }

	/** Seconds left (TimeLimit minus the engine's ElapsedTime); -1 without a limit. */
	int32 GetRemainingTime() const { return TimeLimit > 0 ? FMath::Max(0, TimeLimit * 60 - ElapsedTime) : -1; }

	int32 GetTeamScore(uint8 Team) const { return TeamScores.IsValidIndex(Team) ? TeamScores[Team] : 0; }

	TArray<AMFPlayerState*> GetSortedPlayers() const;
};
