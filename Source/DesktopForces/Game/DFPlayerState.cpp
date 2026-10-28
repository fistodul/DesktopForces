#include "Game/DFPlayerState.h"
#include "Net/UnrealNetwork.h"

void ADFPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADFPlayerState, Team);
	DOREPLIFETIME(ADFPlayerState, PreferredModel);
	DOREPLIFETIME(ADFPlayerState, Kills);
	DOREPLIFETIME(ADFPlayerState, Deaths);
	DOREPLIFETIME(ADFPlayerState, bCaptain);
	DOREPLIFETIME(ADFPlayerState, bAdmin);
	DOREPLIFETIME(ADFPlayerState, DeathTime);
}

void ADFPlayerState::CopyProperties(APlayerState* PlayerState) {
	Super::CopyProperties(PlayerState);
	if (ADFPlayerState* PS = Cast<ADFPlayerState>(PlayerState)) {
		PS->Team = Team;
		PS->PreferredModel = PreferredModel;
		PS->PreferredTeam = PreferredTeam;
		PS->Kills = Kills;
		PS->Deaths = Deaths;
	}
}

void ADFPlayerState::OnRep_Admin() {
	// the owner's cheat manager: its server commands go to the server, which checks again
	if (APlayerController* PC = GetPlayerController(); PC && PC->IsLocalController() && bAdmin)
		PC->AddCheats(true);
}
