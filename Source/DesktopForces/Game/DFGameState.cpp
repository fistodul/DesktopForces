#include "Game/DFGameState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Weapons/DFWeapon.h"
#include "Core/DFGameSettings.h"
#include "Game/DFPlayerState.h"
#include "Net/UnrealNetwork.h"

void ADFGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADFGameState, NumTeams);
	DOREPLIFETIME(ADFGameState, VoteText);
	DOREPLIFETIME(ADFGameState, VoteYes);
	DOREPLIFETIME(ADFGameState, VoteNo);
	DOREPLIFETIME(ADFGameState, VoteEndTime);
	DOREPLIFETIME(ADFGameState, LoadoutAccess);
	DOREPLIFETIME(ADFGameState, TeamLoadoutAccess);
	DOREPLIFETIME(ADFGameState, TeamNameOverrides);
	DOREPLIFETIME(ADFGameState, bCheatsAllowed);
	DOREPLIFETIME(ADFGameState, bParachutes);
	DOREPLIFETIME(ADFGameState, FireRate);
	DOREPLIFETIME(ADFGameState, bInfiniteAmmo);
	DOREPLIFETIME(ADFGameState, BannedItems);
	DOREPLIFETIME(ADFGameState, AllowedItemGroups);
	DOREPLIFETIME(ADFGameState, FragLimit);
	DOREPLIFETIME(ADFGameState, TimeLimit);
	DOREPLIFETIME(ADFGameState, TimeLimitSeconds);
	DOREPLIFETIME(ADFGameState, GoalTeamScore);
	DOREPLIFETIME(ADFGameState, RespawnWait);
	DOREPLIFETIME(ADFGameState, TeamScores);
	DOREPLIFETIME(ADFGameState, WinnerText);
	DOREPLIFETIME(ADFGameState, GameName);
	DOREPLIFETIME(ADFGameState, RoundStartTime);
}

TArray<ADFPlayerState*> ADFGameState::GetSortedPlayers() const
{
	TArray<ADFPlayerState*> Out;
	for (APlayerState* PS : PlayerArray)
	{
		if (ADFPlayerState* MPS = Cast<ADFPlayerState>(PS))
		{
			Out.Add(MPS);
		}
	}
	Out.Sort([](const ADFPlayerState& A, const ADFPlayerState& B)
	{
		return A.GetScore() != B.GetScore() ? A.GetScore() > B.GetScore() : A.Deaths < B.Deaths;
	});
	return Out;
}

FText ADFGameState::GetTeamName(uint8 Team) const
{
	if (TeamNameOverrides.IsValidIndex(Team) && !TeamNameOverrides[Team].IsEmpty())
	{
		return FText::FromString(TeamNameOverrides[Team]);
	}
	return UDFGameSettings::Get().GetTeamName(Team);
}

bool ADFGameState::IsItemAllowed(TSubclassOf<ADFWeapon> Item) const
{
	if (!Item || BannedItems.ContainsByPredicate([Item](const TSubclassOf<ADFWeapon>& B) { return B && Item->IsChildOf(B); }))
	{
		return false;
	}
	return AllowedItemGroups.Num() == 0 || AllowedItemGroups.Contains(Item->GetDefaultObject<ADFWeapon>()->InventoryGroup);
}

void ADFGameState::OnRep_CheatsAllowed()
{
	if (!bCheatsAllowed)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get(); PC && PC->IsLocalController())
		{
			PC->AddCheats(true);
		}
	}
}
