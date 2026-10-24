#include "Game/Captains/Captains.h"
#include "DesktopForces.h"
#include "AI/DFBotController.h"
#include "Game/DFGameState.h"
#include "Game/DFPlayerStart.h"
#include "Game/DFPlayerState.h"
#include "Loadout/DFLoadout.h"
#include "Player/DFCharacter.h"
#include "World/DFMapActors.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

namespace {
	const TCHAR* SquadName(uint8 Team) {
		return Team == 0 ? TEXT("Blue") : TEXT("Red");
	}
} // namespace

ACaptains::ACaptains() {
	GameName = TEXT("Captains");
	MFGameClass = TEXT("RageCaptains");
	InstructionSound = TEXT("Get_their_Captain");
	NumTeams = 2;
	GoalTeamScore = 5; // RageCaptains.GoalTeamScore
	TimeLimit = 12;
	FragLimit = 0;
	bScoreTeamKills = false;
}

void ACaptains::HandleMatchHasStarted() {
	Super::HandleMatchHasStarted();
	ChooseRandomCaptain(0);
	ChooseRandomCaptain(1);
	GetWorldTimerManager().SetTimer(CaptainTimer, this, &ACaptains::CaptainTick, 1.f, true);
}

ADFPlayerState* ACaptains::GetCaptain(uint8 Team) const {
	if (const AGameStateBase* GS = GetGameState<AGameStateBase>()) {
		for (APlayerState* PS : GS->PlayerArray) {
			ADFPlayerState* M = Cast<ADFPlayerState>(PS);
			if (M && M->bCaptain && M->GetTeam() == Team)
				return M;
		}
	}
	return nullptr;
}

void ACaptains::SetCaptain(uint8 Team, ADFPlayerState* NewCaptain) {
	ADFPlayerState* Old = GetCaptain(Team);
	if (Old)
		Old->bCaptain = false;
	if (!NewCaptain)
		return;
	NewCaptain->bCaptain = true;
	if (ADFCharacter* C = Cast<ADFCharacter>(NewCaptain->GetPawn()); C && C->IsAlive())
		C->Heal(C->MaxHealth); // RageCaptains.SetCaptain: Health = 100
	if (ADFBotController* Bot = Cast<ADFBotController>(NewCaptain->GetOwner()))
		Bot->SetOrder(EDFBotOrder::Freelance, nullptr);
	if (NewCaptain != Old)
		BroadcastChat(nullptr, FString::Printf(TEXT("%s is now %s Squad Captain"), *NewCaptain->GetPlayerName(), SquadName(Team)), false);
}

void ACaptains::ChooseRandomCaptain(uint8 Team) {
	SetCaptain(Team, nullptr);
	TArray<ADFPlayerState*> Members;
	if (const AGameStateBase* GS = GetGameState<AGameStateBase>()) {
		for (APlayerState* PS : GS->PlayerArray)
			if (ADFPlayerState* M = Cast<ADFPlayerState>(PS); M && !M->IsSpectator() && M->GetTeam() == Team)
				Members.Add(M);
	}
	if (Members.Num())
		SetCaptain(Team, Members[FMath::RandRange(0, Members.Num() - 1)]);
}

void ACaptains::Logout(AController* Exiting) {
	const ADFPlayerState* PS = Exiting ? Exiting->GetPlayerState<ADFPlayerState>() : nullptr;
	const bool bWasCaptain = PS && PS->bCaptain;
	const uint8 Team = PS ? PS->GetTeam() : DF::NoTeam;
	Super::Logout(Exiting);
	if (bWasCaptain && Team < 2)
		ChooseRandomCaptain(Team);
}

void ACaptains::Killed(AController* Killer, AController* Victim, ADFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType) {
	ADFPlayerState* VPS = Victim ? Victim->GetPlayerState<ADFPlayerState>() : nullptr;
	ADFPlayerState* KPS = Killer ? Killer->GetPlayerState<ADFPlayerState>() : nullptr;
	const bool bCaptain = VPS && VPS->bCaptain;
	Super::Killed(Killer, Victim, VictimPawn, DamageType); // +1 / -1 as usual
	if (!bCaptain || !IsMatchInProgress())
		return;
	const uint8 Team = VPS->GetTeam();
	if (!KPS || KPS == VPS) {
		ChooseRandomCaptain(Team); // RageCaptains.ScoreKill: suicide -> another captain now
		return;
	}
	if (KPS->GetTeam() == Team)
		return; // team kill: stays captain
	// MF: captain kill = team point, 7 for the killer (instead of the usual 1)
	KPS->SetScore(KPS->GetScore() + 6.f);
	ADFGameState* GS = GetGameState<ADFGameState>();
	if (GS && GS->TeamScores.IsValidIndex(KPS->GetTeam()))
		GS->TeamScores[KPS->GetTeam()] += 1;
	BroadcastChat(nullptr, FString::Printf(TEXT("%s killed %s squad captain - %s"), *KPS->GetPlayerName(), Team == 0 ? TEXT("blue") : TEXT("red"), *VPS->GetPlayerName()), false);
	if (GS && GoalTeamScore > 0 && GS->GetTeamScore(KPS->GetTeam()) >= GoalTeamScore) {
		FinishMatch(FString::Printf(TEXT("%s Squad wins!"), SquadName(KPS->GetTeam())));
		return;
	}
	SetCaptain(Team, nullptr);
	if (Team < 2)
		WaitForCaptain[Team] = 0; // StartWaitForNewCaptain
}

AActor* ACaptains::FindCaptainStart(uint8 Team) const {
	for (TActorIterator<ADFPlayerStart> It(GetWorld()); It; ++It)
		if (It->bCaptainStart && It->TeamNumber == Team)
			return *It;
	return nullptr;
}

AActor* ACaptains::ChoosePlayerStart_Implementation(AController* Player) {
	// RageCaptains.FindPlayerStart: the captain spawns at the team's CaptainStart
	if (const ADFPlayerState* PS = Player ? Player->GetPlayerState<ADFPlayerState>() : nullptr; PS && PS->bCaptain) {
		if (AActor* Start = FindCaptainStart(PS->GetTeam()))
			return Start;
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}

void ACaptains::CaptainTick() {
	if (!IsMatchInProgress())
		return;
	for (uint8 T = 0; T < 2; ++T) {
		if (WaitForCaptain[T] >= 0 && ++WaitForCaptain[T] > PauseBetweenCaptains) {
			WaitForCaptain[T] = -1;
			ChooseRandomCaptain(T);
		}
		// no hiding in the start rooms (MF: LoadoutZone / CaptainProhibitedZone -> CaptainStart)
		const ADFPlayerState* Cap = GetCaptain(T);
		ADFCharacter* C = Cap ? Cast<ADFCharacter>(Cap->GetPawn()) : nullptr;
		if (!C || !C->IsAlive())
			continue;
		bool bForbidden = false;
		for (TActorIterator<ALoadoutZone> It(GetWorld()); It && !bForbidden; ++It)
			bForbidden = It->IsInside(C);
		for (TActorIterator<ADFZoneBox> It(GetWorld()); It && !bForbidden; ++It)
			bForbidden = It->ActorHasTag(TEXT("CaptainProhibited")) && It->IsInside(C->GetActorLocation());
		if (bForbidden) {
			if (AActor* Start = FindCaptainStart(T)) {
				C->TeleportTo(Start->GetActorLocation(), Start->GetActorRotation());
				if (AController* PC = C->GetController())
					PC->SetControlRotation(Start->GetActorRotation());
			}
		}
	}
}

FString ACaptains::GetOrderString(int32 Order) const {
	// Rage.int OrderString 5 / 6
	if (Order == 0)
		return TEXT("Support the captain.");
	if (Order == 2)
		return TEXT("Seek and destroy.");
	return Super::GetOrderString(Order);
}

void ACaptains::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const {
	const ADFPlayerState* PS = Bot ? Bot->GetPlayerState<ADFPlayerState>() : nullptr;
	if (!PS || PS->GetTeam() > 1)
		return;
	const ADFPlayerState* Enemy = GetCaptain(1 - PS->GetTeam());
	const ADFPlayerState* Own = GetCaptain(PS->GetTeam());
	const ADFBotController* B = Cast<ADFBotController>(Bot);
	const bool bSupport = B && B->GetOrder() == EDFBotOrder::Defend;
	if (PS->bCaptain)
		return; // the captain roams freely (MF SetCaptain: Freelance)
	if (bSupport && Own && Own->GetPawn())
		OutGoals.Add(Own->GetPawn());
	if (Enemy && Enemy->GetPawn())
		OutGoals.Add(Enemy->GetPawn()); // hunt their captain
	if (!bSupport && Own && Own->GetPawn())
		OutGoals.Add(Own->GetPawn());
}

FName ACaptains::GetOrderSound(int32 Order) const {
	// RageVoicePack.SetOrderMessage: OrderSound 5 / 6
	return Order == 0 ? FName(TEXT("Support_the_Captain")) : Order == 2 ? FName(TEXT("Seek_and_destroy"))
																		: Super::GetOrderSound(Order);
}
