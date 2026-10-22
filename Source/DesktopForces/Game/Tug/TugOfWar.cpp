#include "Game/Tug/TugOfWar.h"
#include "Core/DFGameSettings.h"
#include "Game/DFGameState.h"
#include "Game/DFMessages.h"
#include "Game/DFPlayerState.h"
#include "Player/DFCharacter.h"
#include "Engine/World.h"
#include "TimerManager.h"

ATugOfWar::ATugOfWar()
{
	GameName = TEXT("Tug of War");
	MFGameClass = TEXT("TugGame");
	NumTeams = 2;
	GoalTeamScore = 3;      // TugGame FragLimit 3 (rounds)
	TimeLimit = 8;          // per round
	FragLimit = 0;
	bScoreTeamKills = false;
}

void ATugOfWar::RestartPlayer(AController* NewPlayer)
{
	Super::RestartPlayer(NewPlayer);
	// TugGame.AddToTeam -> SetInitialTeam: the team someone first played for
	if (const ADFPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<ADFPlayerState>() : nullptr; PS && !InitialTeams.Contains(NewPlayer))
	{
		InitialTeams.Add(NewPlayer, PS->GetTeam());
	}
}

void ATugOfWar::Logout(AController* Exiting)
{
	const ADFPlayerState* PS = Exiting ? Exiting->GetPlayerState<ADFPlayerState>() : nullptr;
	const uint8 Team = PS ? PS->GetTeam() : DF::NoTeam;
	InitialTeams.Remove(Exiting);
	Super::Logout(Exiting);
	if (IsMatchInProgress() && !bRoundOver && Team < 2 && TeamSize(Team) == 0 && TeamSize(1 - Team) > 0)
	{
		RoundEnded(1 - Team);
	}
}

int32 ATugOfWar::TeamSize(uint8 Team) const
{
	int32 N = 0;
	if (const AGameStateBase* GS = GetGameState<AGameStateBase>())
	{
		for (const APlayerState* PS : GS->PlayerArray)
		{
			const ADFPlayerState* M = Cast<ADFPlayerState>(PS);
			N += M && !M->IsSpectator() && M->GetTeam() == Team;
		}
	}
	return N;
}

bool ATugOfWar::PreventDeath(ADFCharacter* Victim, AController* Killer, TSubclassOf<UDamageType> DamageType)
{
	ADFPlayerState* KPS = Killer ? Killer->GetPlayerState<ADFPlayerState>() : nullptr;
	ADFPlayerState* VPS = Victim ? Victim->GetPlayerState<ADFPlayerState>() : nullptr;
	if (!bKillTransform || bRoundOver || !IsMatchInProgress() || !KPS || !VPS || KPS == VPS || KPS->GetTeam() == VPS->GetTeam() || KPS->GetTeam() > 1)
	{
		return false;   // suicides, falls and team kills die as usual
	}
	// TugPlayer.Died: back to full health, scored like a kill, now on the killer's side
	ScoreKill(KPS, VPS);
	BroadcastLocalized(this, UDFDeathMessage::StaticClass(), 0, KPS, VPS, DamageType.Get());
	const uint8 OldTeam = VPS->GetTeam();
	VPS->SetTeam(KPS->GetTeam());
	Victim->Heal(Victim->MaxHealth);
	BroadcastChat(nullptr, FString::Printf(TEXT("%s joined the %s team"), *VPS->GetPlayerName(),
		*GetGameState<ADFGameState>()->GetTeamName(KPS->GetTeam()).ToString().ToLower()), false);
	// TugGame.MovedTeam: their old team is gone -> round to the converter's team
	if (TeamSize(OldTeam) == 0)
	{
		KPS->SetScore(KPS->GetScore() + 2.f);
		RoundEnded(KPS->GetTeam());
	}
	return true;
}

void ATugOfWar::OnTimeLimit()
{
	if (!bRoundOver)
	{
		RoundEnded(-1);   // TugGame.EndGame("timelimit")
	}
}

void ATugOfWar::RoundEnded(int32 Winner)
{
	if (Winner < 0)
	{
		const int32 A = TeamSize(0), B = TeamSize(1);
		Winner = A > B ? 0 : (B > A ? 1 : FMath::RandRange(0, 1));
	}
	ADFGameState* GS = GetGameState<ADFGameState>();
	if (!GS || !GS->TeamScores.IsValidIndex(Winner))
	{
		return;
	}
	bRoundOver = true;
	GS->TeamScores[Winner] += 1;
	const FString Team = GS->GetTeamName(Winner).ToString();
	if (GoalTeamScore > 0 && GS->TeamScores[Winner] >= GoalTeamScore)
	{
		FinishMatch(FString::Printf(TEXT("%s Team Wins!"), *Team));
		return;
	}
	BroadcastChat(nullptr, FString::Printf(TEXT("%s team wins the round!"), *Team), false);
	GetWorldTimerManager().SetTimer(RoundTimer, this, &ATugOfWar::NextRound, 3.f, false);
}

void ATugOfWar::NextRound()
{
	// TugGame.RestartRound: everyone back on the team they started on, fresh loadout
	for (auto It = InitialTeams.CreateIterator(); It; ++It)
	{
		ADFPlayerState* PS = It->Key.IsValid() ? It->Key->GetPlayerState<ADFPlayerState>() : nullptr;
		if (!PS)
		{
			It.RemoveCurrent();
			continue;
		}
		PS->SetTeam(It->Value);
	}
	bRoundOver = false;
	RestartRound();
}
