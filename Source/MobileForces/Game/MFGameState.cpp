#include "Game/MFGameState.h"
#include "Game/MFPlayerState.h"
#include "Net/UnrealNetwork.h"

void AMFGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFGameState, NumTeams);
	DOREPLIFETIME(AMFGameState, FragLimit);
	DOREPLIFETIME(AMFGameState, TimeLimit);
	DOREPLIFETIME(AMFGameState, TimeLimitSeconds);
	DOREPLIFETIME(AMFGameState, GoalTeamScore);
	DOREPLIFETIME(AMFGameState, RespawnWait);
	DOREPLIFETIME(AMFGameState, TeamScores);
	DOREPLIFETIME(AMFGameState, WinnerText);
	DOREPLIFETIME(AMFGameState, GameName);
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
