#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Core/MFTypes.h"
#include "MFGameState.generated.h"

class AMFPlayerState;

/** One line of the kill feed. */
USTRUCT()
struct FMFKillMessage
{
	GENERATED_BODY()
	UPROPERTY() FString Killer;
	UPROPERTY() FString Victim;
	UPROPERTY() FString Verb;
	UPROPERTY() float Time = 0.f;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FMFOnMessage, const FString&);

/**
 * MF GameReplicationInfo (RageGameRepInfo / RageTeamReplicationInfo): rules, clock,
 * team scores, end-of-match state. The server ticks the clock once per second (timer),
 * clients count down locally between updates.
 */
UCLASS()
class MOBILEFORCES_API AMFGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	bool bTeamGame = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	int32 FragLimit = 0;

	/** Minutes (0 = none). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	int32 TimeLimit = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	int32 GoalTeamScore = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	int32 RemainingTime = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	int32 RespawnWait = 1;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	TArray<int32> TeamScores = { 0, 0 };

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	bool bMatchOver = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	FString WinnerText;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	FString GameName;

	/** Broadcast a kill to everyone (kill feed). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastKill(const FString& Killer, const FString& Victim, const FString& Verb);

	/** Broadcast a centre-screen announcement (first blood, sprees, match end). */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastAnnounce(const FString& Text);

	TArray<FMFKillMessage> KillFeed;
	FString Announcement;
	float AnnouncementTime = -100.f;

	TArray<AMFPlayerState*> GetSortedPlayers() const;
};
