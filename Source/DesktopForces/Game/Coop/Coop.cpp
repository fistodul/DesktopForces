#include "Game/Coop/Coop.h"
#include "Game/DFGameState.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

#define LOCTEXT_NAMESPACE "DFCoop"

ACoop::ACoop() {
	GameName = TEXT("Coop mode");
	MFGameClass = TEXT("CoopMode");
	GoalTeamScore = 3; // CoopMode.FragLimit 3 (captures)
	TimeLimit = 0; // CoopMode.TimeLimit 0
	BotCount = 12; // MF MinPlayers 100 filled every start; 12 keeps low-end PCs happy (?Bots=N)
}

void ACoop::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) {
	Super::InitGame(MapName, Options, ErrorMessage);
	Weapons = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Weapons"), Weapons), 0, 4);
}

void ACoop::InitGameState() {
	Super::InitGameState();
	if (ADFGameState* GS = GetGameState<ADFGameState>())
		GS->LoadoutAccess = Weapons;
}

uint8 ACoop::ChooseTeam(AController* Who, uint8 Preferred) const {
	return Who && Who->IsPlayerController() ? 1 : 0;
}

void ACoop::LoadoutDenied(APlayerController* PC) const {
	if (PC)
		PC->ClientMessage(LOCTEXT("NiceTry", "Nice try, human!").ToString()); // CoopPlayer.TryLoadout
}

#undef LOCTEXT_NAMESPACE
