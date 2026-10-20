#include "MFGameMode_Tug.h"
#include "Core/MFGameSettings.h"
#include "Game/MFGameState.h"
#include "Game/MFMessages.h"
#include "Game/MFPlayerState.h"
#include "Player/MFCharacter.h"
#include "Engine/World.h"
#include "TimerManager.h"

AMFGameMode_Tug::AMFGameMode_Tug()
{
	GameName = TEXT("Tug of War");
	NumTeams = 2;
	GoalTeamScore = 3;      // TugGame FragLimit 3 (rounds)
	TimeLimit = 8;          // per round
	FragLimit = 0;
	bScoreTeamKills = false;
}

void AMFGameMode_Tug::RestartPlayer(AController* NewPlayer)
{
	Super::RestartPlayer(NewPlayer);
	// TugGame.AddToTeam -> SetInitialTeam: the team someone first played for
	if (const AMFPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<AMFPlayerState>() : nullptr; PS && !InitialTeams.Contains(NewPlayer))
	{
		InitialTeams.Add(NewPlayer, PS->GetTeam());
	}
}

void AMFGameMode_Tug::Logout(AController* Exiting)
{
	const AMFPlayerState* PS = Exiting ? Exiting->GetPlayerState<AMFPlayerState>() : nullptr;
	const uint8 Team = PS ? PS->GetTeam() : MF::NoTeam;
	InitialTeams.Remove(Exiting);
	Super::Logout(Exiting);
	if (IsMatchInProgress() && !bRoundOver && Team < 2 && TeamSize(Team) == 0 && TeamSize(1 - Team) > 0)
	{
		RoundEnded(1 - Team);
	}
}

int32 AMFGameMode_Tug::TeamSize(uint8 Team) const
{
	int32 N = 0;
	if (const AGameStateBase* GS = GetGameState<AGameStateBase>())
	{
		for (const APlayerState* PS : GS->PlayerArray)
		{
			const AMFPlayerState* M = Cast<AMFPlayerState>(PS);
			N += M && !M->IsSpectator() && M->GetTeam() == Team;
		}
	}
	return N;
}

bool AMFGameMode_Tug::PreventDeath(AMFCharacter* Victim, AController* Killer, TSubclassOf<UDamageType> DamageType)
{
	AMFPlayerState* KPS = Killer ? Killer->GetPlayerState<AMFPlayerState>() : nullptr;
	AMFPlayerState* VPS = Victim ? Victim->GetPlayerState<AMFPlayerState>() : nullptr;
	if (!bKillTransform || bRoundOver || !IsMatchInProgress() || !KPS || !VPS || KPS == VPS || KPS->GetTeam() == VPS->GetTeam() || KPS->GetTeam() > 1)
	{
		return false;   // suicides, falls and team kills die as usual
	}
	// TugPlayer.Died: back to full health, scored like a kill, now on the killer's side
	ScoreKill(KPS, VPS);
	BroadcastLocalized(this, UMFDeathMessage::StaticClass(), 0, KPS, VPS, DamageType.Get());
	const uint8 OldTeam = VPS->GetTeam();
	VPS->SetTeam(KPS->GetTeam());
	Victim->Heal(Victim->MaxHealth);
	BroadcastChat(nullptr, FString::Printf(TEXT("%s joined the %s team"), *VPS->GetPlayerName(),
		*UMFGameSettings::Get().GetTeamName(KPS->GetTeam()).ToString().ToLower()), false);
	// TugGame.MovedTeam: their old team is gone -> round to the converter's team
	if (TeamSize(OldTeam) == 0)
	{
		KPS->SetScore(KPS->GetScore() + 2.f);
		RoundEnded(KPS->GetTeam());
	}
	return true;
}

void AMFGameMode_Tug::OnTimeLimit()
{
	if (!bRoundOver)
	{
		RoundEnded(-1);   // TugGame.EndGame("timelimit")
	}
}

void AMFGameMode_Tug::RoundEnded(int32 Winner)
{
	if (Winner < 0)
	{
		const int32 A = TeamSize(0), B = TeamSize(1);
		Winner = A > B ? 0 : (B > A ? 1 : FMath::RandRange(0, 1));
	}
	AMFGameState* GS = GetGameState<AMFGameState>();
	if (!GS || !GS->TeamScores.IsValidIndex(Winner))
	{
		return;
	}
	bRoundOver = true;
	GS->TeamScores[Winner] += 1;
	const FString Team = UMFGameSettings::Get().GetTeamName(Winner).ToString();
	if (GoalTeamScore > 0 && GS->TeamScores[Winner] >= GoalTeamScore)
	{
		FinishMatch(FString::Printf(TEXT("%s Team Wins!"), *Team));
		return;
	}
	BroadcastChat(nullptr, FString::Printf(TEXT("%s team wins the round!"), *Team), false);
	GetWorldTimerManager().SetTimer(RoundTimer, this, &AMFGameMode_Tug::NextRound, 3.f, false);
}

void AMFGameMode_Tug::NextRound()
{
	// TugGame.RestartRound: everyone back on the team they started on, fresh loadout
	for (auto It = InitialTeams.CreateIterator(); It; ++It)
	{
		AMFPlayerState* PS = It->Key.IsValid() ? It->Key->GetPlayerState<AMFPlayerState>() : nullptr;
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
