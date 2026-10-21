#include "Game/MFGameState.h"
#include "Weapons/MFWeapon.h"
#include "Core/MFGameSettings.h"
#include "Game/MFPlayerState.h"
#include "Net/UnrealNetwork.h"

void AMFGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFGameState, NumTeams);
	DOREPLIFETIME(AMFGameState, VoteText);
	DOREPLIFETIME(AMFGameState, VoteYes);
	DOREPLIFETIME(AMFGameState, VoteNo);
	DOREPLIFETIME(AMFGameState, VoteEndTime);
	DOREPLIFETIME(AMFGameState, LoadoutAccess);
	DOREPLIFETIME(AMFGameState, TeamLoadoutAccess);
	DOREPLIFETIME(AMFGameState, TeamNameOverrides);
	DOREPLIFETIME(AMFGameState, AllowedItemGroups);
	DOREPLIFETIME(AMFGameState, FragLimit);
	DOREPLIFETIME(AMFGameState, TimeLimit);
	DOREPLIFETIME(AMFGameState, TimeLimitSeconds);
	DOREPLIFETIME(AMFGameState, GoalTeamScore);
	DOREPLIFETIME(AMFGameState, RespawnWait);
	DOREPLIFETIME(AMFGameState, TeamScores);
	DOREPLIFETIME(AMFGameState, WinnerText);
	DOREPLIFETIME(AMFGameState, GameName);
	DOREPLIFETIME(AMFGameState, RoundStartTime);
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

FText AMFGameState::GetTeamName(uint8 Team) const
{
	if (TeamNameOverrides.IsValidIndex(Team) && !TeamNameOverrides[Team].IsEmpty())
	{
		return FText::FromString(TeamNameOverrides[Team]);
	}
	return UMFGameSettings::Get().GetTeamName(Team);
}

bool AMFGameState::IsItemAllowed(TSubclassOf<AMFWeapon> Item) const
{
	return AllowedItemGroups.Num() == 0 || (Item && AllowedItemGroups.Contains(Item->GetDefaultObject<AMFWeapon>()->InventoryGroup));
}
