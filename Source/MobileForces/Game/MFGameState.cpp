#include "Game/MFGameState.h"
#include "Game/MFPlayerState.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

void AMFGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFGameState, bTeamGame);
	DOREPLIFETIME(AMFGameState, FragLimit);
	DOREPLIFETIME(AMFGameState, TimeLimit);
	DOREPLIFETIME(AMFGameState, GoalTeamScore);
	DOREPLIFETIME(AMFGameState, RemainingTime);
	DOREPLIFETIME(AMFGameState, RespawnWait);
	DOREPLIFETIME(AMFGameState, TeamScores);
	DOREPLIFETIME(AMFGameState, bMatchOver);
	DOREPLIFETIME(AMFGameState, WinnerText);
	DOREPLIFETIME(AMFGameState, GameName);
}

void AMFGameState::MulticastKill_Implementation(const FString& Killer, const FString& Victim, const FString& Verb)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	FMFKillMessage M;
	M.Killer = Killer;
	M.Victim = Victim;
	M.Verb = Verb;
	M.Time = GetWorld()->GetTimeSeconds();
	KillFeed.Add(M);
	if (KillFeed.Num() > 4)
	{
		KillFeed.RemoveAt(0);
	}
}

void AMFGameState::MulticastAnnounce_Implementation(const FString& Text)
{
	Announcement = Text;
	AnnouncementTime = GetWorld()->GetTimeSeconds();
}

TArray<AMFPlayerState*> AMFGameState::GetSortedPlayers() const
{
	TArray<AMFPlayerState*> Out;
	for (APlayerState* PS : PlayerArray)
	{
		if (AMFPlayerState* MPS = Cast<AMFPlayerState>(PS))
		{
			Out.Add(MPS);
		}
	}
	Out.Sort([](const AMFPlayerState& A, const AMFPlayerState& B)
	{
		return A.GetScore() != B.GetScore() ? A.GetScore() > B.GetScore() : A.Deaths < B.Deaths;
	});
	return Out;
}
