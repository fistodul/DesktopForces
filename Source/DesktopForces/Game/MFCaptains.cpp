#include "Game/MFCaptains.h"
#include "DesktopForces.h"
#include "AI/MFBotController.h"
#include "Game/MFGameState.h"
#include "Game/MFPlayerStart.h"
#include "Game/MFPlayerState.h"
#include "Loadout/MFLoadout.h"
#include "Player/MFCharacter.h"
#include "World/MFMapActors.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

namespace
{
	const TCHAR* SquadName(uint8 Team) { return Team == 0 ? TEXT("Blue") : TEXT("Red"); }
}

AMFGameMode_Captains::AMFGameMode_Captains()
{
	GameName = TEXT("Captains");
	MFGameClass = TEXT("RageCaptains");
	InstructionSound = TEXT("Get_their_Captain");
	NumTeams = 2;
	GoalTeamScore = 5;      // RageCaptains.GoalTeamScore
	TimeLimit = 12;
	FragLimit = 0;
	bScoreTeamKills = false;
}

void AMFGameMode_Captains::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();
	ChooseRandomCaptain(0);
	ChooseRandomCaptain(1);
	GetWorldTimerManager().SetTimer(CaptainTimer, this, &AMFGameMode_Captains::CaptainTick, 1.f, true);
}

AMFPlayerState* AMFGameMode_Captains::GetCaptain(uint8 Team) const
{
	if (const AGameStateBase* GS = GetGameState<AGameStateBase>())
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			AMFPlayerState* M = Cast<AMFPlayerState>(PS);
			if (M && M->bCaptain && M->GetTeam() == Team)
			{
				return M;
			}
		}
	}
	return nullptr;
}

void AMFGameMode_Captains::SetCaptain(uint8 Team, AMFPlayerState* NewCaptain)
{
	AMFPlayerState* Old = GetCaptain(Team);
	if (Old)
	{
		Old->bCaptain = false;
	}
	if (!NewCaptain)
	{
		return;
	}
	NewCaptain->bCaptain = true;
	if (AMFCharacter* C = Cast<AMFCharacter>(NewCaptain->GetPawn()); C && C->IsAlive())
	{
		C->Heal(C->MaxHealth);   // RageCaptains.SetCaptain: Health = 100
	}
	if (AMFBotController* Bot = Cast<AMFBotController>(NewCaptain->GetOwner()))
	{
		Bot->SetOrder(EMFBotOrder::Freelance, nullptr);
	}
	if (NewCaptain != Old)
	{
		BroadcastChat(nullptr, FString::Printf(TEXT("%s is now %s Squad Captain"), *NewCaptain->GetPlayerName(), SquadName(Team)), false);
	}
}

void AMFGameMode_Captains::ChooseRandomCaptain(uint8 Team)
{
	SetCaptain(Team, nullptr);
	TArray<AMFPlayerState*> Members;
	if (const AGameStateBase* GS = GetGameState<AGameStateBase>())
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			if (AMFPlayerState* M = Cast<AMFPlayerState>(PS); M && !M->IsSpectator() && M->GetTeam() == Team)
			{
				Members.Add(M);
			}
		}
	}
	if (Members.Num())
	{
		SetCaptain(Team, Members[FMath::RandRange(0, Members.Num() - 1)]);
	}
}

void AMFGameMode_Captains::Logout(AController* Exiting)
{
	const AMFPlayerState* PS = Exiting ? Exiting->GetPlayerState<AMFPlayerState>() : nullptr;
	const bool bWasCaptain = PS && PS->bCaptain;
	const uint8 Team = PS ? PS->GetTeam() : MF::NoTeam;
	Super::Logout(Exiting);
	if (bWasCaptain && Team < 2)
	{
		ChooseRandomCaptain(Team);
	}
}

void AMFGameMode_Captains::Killed(AController* Killer, AController* Victim, AMFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType)
{
	AMFPlayerState* VPS = Victim ? Victim->GetPlayerState<AMFPlayerState>() : nullptr;
	AMFPlayerState* KPS = Killer ? Killer->GetPlayerState<AMFPlayerState>() : nullptr;
	const bool bCaptain = VPS && VPS->bCaptain;
	Super::Killed(Killer, Victim, VictimPawn, DamageType);   // +1 / -1 as usual
	if (!bCaptain || !IsMatchInProgress())
	{
		return;
	}
	const uint8 Team = VPS->GetTeam();
	if (!KPS || KPS == VPS)
	{
		ChooseRandomCaptain(Team);   // RageCaptains.ScoreKill: suicide -> another captain now
		return;
	}
	if (KPS->GetTeam() == Team)
	{
		return;   // team kill: stays captain
	}
	// MF: captain kill = team point, 7 for the killer (instead of the usual 1)
	KPS->SetScore(KPS->GetScore() + 6.f);
	AMFGameState* GS = GetGameState<AMFGameState>();
	if (GS && GS->TeamScores.IsValidIndex(KPS->GetTeam()))
	{
		GS->TeamScores[KPS->GetTeam()] += 1;
	}
	BroadcastChat(nullptr, FString::Printf(TEXT("%s killed %s squad captain - %s"), *KPS->GetPlayerName(),
		Team == 0 ? TEXT("blue") : TEXT("red"), *VPS->GetPlayerName()), false);
	if (GS && GoalTeamScore > 0 && GS->GetTeamScore(KPS->GetTeam()) >= GoalTeamScore)
	{
		FinishMatch(FString::Printf(TEXT("%s Squad wins!"), SquadName(KPS->GetTeam())));
		return;
	}
	SetCaptain(Team, nullptr);
	if (Team < 2)
	{
		WaitForCaptain[Team] = 0;   // StartWaitForNewCaptain
	}
}

AActor* AMFGameMode_Captains::FindCaptainStart(uint8 Team) const
{
	for (TActorIterator<AMFPlayerStart> It(GetWorld()); It; ++It)
	{
		if (It->bCaptainStart && It->TeamNumber == Team)
		{
			return *It;
		}
	}
	return nullptr;
}

AActor* AMFGameMode_Captains::ChoosePlayerStart_Implementation(AController* Player)
{
	// RageCaptains.FindPlayerStart: the captain spawns at the team's CaptainStart
	if (const AMFPlayerState* PS = Player ? Player->GetPlayerState<AMFPlayerState>() : nullptr; PS && PS->bCaptain)
	{
		if (AActor* Start = FindCaptainStart(PS->GetTeam()))
		{
			return Start;
		}
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}

void AMFGameMode_Captains::CaptainTick()
{
	if (!IsMatchInProgress())
	{
		return;
	}
	for (uint8 T = 0; T < 2; ++T)
	{
		if (WaitForCaptain[T] >= 0 && ++WaitForCaptain[T] > PauseBetweenCaptains)
		{
			WaitForCaptain[T] = -1;
			ChooseRandomCaptain(T);
		}
		// no hiding in the start rooms (MF: LoadoutZone / CaptainProhibitedZone -> CaptainStart)
		const AMFPlayerState* Cap = GetCaptain(T);
		AMFCharacter* C = Cap ? Cast<AMFCharacter>(Cap->GetPawn()) : nullptr;
		if (!C || !C->IsAlive())
		{
			continue;
		}
		bool bForbidden = false;
		for (TActorIterator<AMFLoadoutZone> It(GetWorld()); It && !bForbidden; ++It)
		{
			bForbidden = It->IsInside(C);
		}
		for (TActorIterator<AMFZoneBox> It(GetWorld()); It && !bForbidden; ++It)
		{
			bForbidden = It->ActorHasTag(TEXT("CaptainProhibited")) && It->IsInside(C->GetActorLocation());
		}
		if (bForbidden)
		{
			if (AActor* Start = FindCaptainStart(T))
			{
				C->TeleportTo(Start->GetActorLocation(), Start->GetActorRotation());
				if (AController* PC = C->GetController())
				{
					PC->SetControlRotation(Start->GetActorRotation());
				}
			}
		}
	}
}

FString AMFGameMode_Captains::GetOrderString(int32 Order) const
{
	// Rage.int OrderString 5 / 6
	if (Order == 0)
	{
		return TEXT("Support the captain.");
	}
	if (Order == 2)
	{
		return TEXT("Seek and destroy.");
	}
	return Super::GetOrderString(Order);
}

void AMFGameMode_Captains::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const
{
	const AMFPlayerState* PS = Bot ? Bot->GetPlayerState<AMFPlayerState>() : nullptr;
	if (!PS || PS->GetTeam() > 1)
	{
		return;
	}
	const AMFPlayerState* Enemy = GetCaptain(1 - PS->GetTeam());
	const AMFPlayerState* Own = GetCaptain(PS->GetTeam());
	const AMFBotController* B = Cast<AMFBotController>(Bot);
	const bool bSupport = B && B->GetOrder() == EMFBotOrder::Defend;
	if (PS->bCaptain)
	{
		return;   // the captain roams freely (MF SetCaptain: Freelance)
	}
	if (bSupport && Own && Own->GetPawn())
	{
		OutGoals.Add(Own->GetPawn());
	}
	if (Enemy && Enemy->GetPawn())
	{
		OutGoals.Add(Enemy->GetPawn());   // hunt their captain
	}
	if (!bSupport && Own && Own->GetPawn())
	{
		OutGoals.Add(Own->GetPawn());
	}
}

FName AMFGameMode_Captains::GetOrderSound(int32 Order) const
{
	// RageVoicePack.SetOrderMessage: OrderSound 5 / 6
	return Order == 0 ? FName(TEXT("Support_the_Captain")) : Order == 2 ? FName(TEXT("Seek_and_destroy")) : Super::GetOrderSound(Order);
}
