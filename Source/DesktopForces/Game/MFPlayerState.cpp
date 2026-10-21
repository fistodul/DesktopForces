#include "Game/MFPlayerState.h"
#include "Net/UnrealNetwork.h"

void AMFPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFPlayerState, Team);
	DOREPLIFETIME(AMFPlayerState, Kills);
	DOREPLIFETIME(AMFPlayerState, Deaths);
	DOREPLIFETIME(AMFPlayerState, bCaptain);
	DOREPLIFETIME(AMFPlayerState, bAdmin);
	DOREPLIFETIME(AMFPlayerState, DeathTime);
}

void AMFPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);
	if (AMFPlayerState* PS = Cast<AMFPlayerState>(PlayerState))
	{
		PS->Team = Team;
		PS->Kills = Kills;
		PS->Deaths = Deaths;
	}
}

void AMFPlayerState::OnRep_Admin()
{
	// the owner's cheat manager: its server commands go to the server, which checks again
	if (APlayerController* PC = GetPlayerController(); PC && PC->IsLocalController() && bAdmin)
	{
		PC->AddCheats(true);
	}
}
