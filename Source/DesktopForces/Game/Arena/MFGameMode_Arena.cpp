#include "Game/Arena/MFGameMode_Arena.h"
#include "Game/MFGameState.h"
#include "Game/MFPlayerState.h"
#include "Player/MFCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

AMFGameMode_Arena::AMFGameMode_Arena()
{
	GameName = TEXT("Arena");
	NumTeams = 2;
	GoalTeamScore = 5;          // FragLimit 5 (rounds)
	TimeLimit = 10;
	bScoreTeamKills = false;
}

void AMFGameMode_Arena::HandleMatchHasStarted()
{
	UnlockLeft = TimeUntilUnlock;
	Super::HandleMatchHasStarted();
}

int32 AMFGameMode_Arena::AliveCount(uint8 Team) const
{
	int32 N = 0;
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
	{
		N += It->IsAlive() && It->GetTeam() == Team;
	}
	return N;
}

void AMFGameMode_Arena::RestartPlayer(AController* NewPlayer)
{
	// out for the rest of the round (MF: playerWaiting until doRestartRound)
	if (bDisableRespawn && !bRestartingRound && DeadThisRound.Contains(NewPlayer))
	{
		return;
	}
	Super::RestartPlayer(NewPlayer);
}

void AMFGameMode_Arena::Killed(AController* Killer, AController* Victim, AMFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType)
{
	Super::Killed(Killer, Victim, VictimPawn, DamageType);
	if (Victim)
	{
		DeadThisRound.Add(Victim);
	}
	const uint8 Team = VictimPawn ? VictimPawn->GetTeam() : MF::NoTeam;
	if (bRoundOver || Team > 1 || AliveCount(Team) > 0)
	{
		return;
	}
	// the last of their team: the round to the others
	const AMFPlayerState* KPS = Killer ? Killer->GetPlayerState<AMFPlayerState>() : nullptr;
	RoundEnd(KPS && KPS->GetTeam() <= 1 && KPS->GetTeam() != Team ? KPS->GetTeam() : uint8(1 - Team), false);
}

void AMFGameMode_Arena::Logout(AController* Exiting)
{
	DeadThisRound.Remove(Exiting);
	Super::Logout(Exiting);
}

void AMFGameMode_Arena::UpdatePoints()
{
	AMFGameState* GS = GetGameState<AMFGameState>();
	if (!GS || !IsMatchInProgress() || bRoundOver || Points.Num() == 0)
	{
		return;
	}
	AMFHoldoutPoint* P = Points[0];
	// arenaModeTimer.Timer: locked for TimeUntilUnlock seconds, counted down at the end
	if (UnlockLeft > 0)
	{
		--UnlockLeft;
		if (UnlockLeft == 10)
		{
			Event(TEXT("Get ready"), MF::NoTeam);
		}
		else if (UnlockLeft > 0 && UnlockLeft <= 5)
		{
			Event(FString::FromInt(UnlockLeft), MF::NoTeam);
		}
		else if (UnlockLeft == 0)
		{
			Event(TEXT("Point unlocked"), MF::NoTeam);
		}
		P->SetOwnerTeam(MF::NoTeam);
		return;
	}
	// who's on it (CollisionRadius / Height 60 uu)
	bool bOn[2] = { false, false };
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
	{
		const FVector D = It->GetActorLocation() - P->GetActorLocation();
		if (It->IsAlive() && It->GetTeam() <= 1 && FVector2D(D).Size() < 135.f + 40.f && FMath::Abs(D.Z) < 135.f + 90.f)
		{
			bOn[It->GetTeam()] = true;
		}
	}
	if (CapturingTeam <= 1 && !bOn[CapturingTeam])
	{
		CapturingTeam = MF::NoTeam;   // unTouch: control lapses, the count starts over
		CaptureProgress = 0;
	}
	for (uint8 T = 0; T < 2; ++T)
	{
		if (bOn[T] && !bOn[1 - T] && CapturingTeam != T)
		{
			CapturingTeam = T;        // touch by the other team: theirs now, from zero
			CaptureProgress = 0;
			if (GetWorld()->GetTimeSeconds() - LastAnnounce > 5.f)
			{
				LastAnnounce = GetWorld()->GetTimeSeconds();
				Event(FString::Printf(TEXT("%s team is taking the point"), *GS->GetTeamName(T).ToString()), T);
			}
		}
	}
	if (CapturingTeam <= 1 && !bOn[1 - CapturingTeam])
	{
		++CaptureProgress;            // UpdateTimer: contested points don't count
	}
	P->SetOwnerTeam(CapturingTeam);
	for (int32 T = 0; T < P->TeamTimes.Num(); ++T)
	{
		P->TeamTimes[T] = T == CapturingTeam ? CaptureProgress : 0;
	}
	if (CapturingTeam <= 1 && CaptureProgress >= TimeUntilCapture)
	{
		RoundEnd(CapturingTeam, true);   // TimerLimit
	}
}

void AMFGameMode_Arena::RoundEnd(uint8 Winner, bool bCaptured)
{
	AMFGameState* GS = GetGameState<AMFGameState>();
	if (!GS || !GS->TeamScores.IsValidIndex(Winner) || bRoundOver)
	{
		return;
	}
	bRoundOver = true;
	GS->TeamScores[Winner] += 1;
	const FString Team = GS->GetTeamName(Winner).ToString();
	if (GS->TeamScores[Winner] >= GoalTeamScore)
	{
		FinishMatch(FString::Printf(TEXT("%s Team Wins!"), *Team));
		return;
	}
	Event(bCaptured ? FString::Printf(TEXT("%s team captured the point!"), *Team) : FString::Printf(TEXT("%s team wins the round!"), *Team), Winner);
	GetWorldTimerManager().SetTimer(RoundTimer, this, &AMFGameMode_Arena::NextRound, 3.f, false);
}

void AMFGameMode_Arena::NextRound()
{
	// doRestartRound: everyone back, the point locked again
	DeadThisRound.Reset();
	CapturingTeam = MF::NoTeam;
	CaptureProgress = 0;
	UnlockLeft = TimeUntilUnlock;
	bRoundOver = false;
	TGuardValue<bool> Restarting(bRestartingRound, true);
	RestartRound();
}

void AMFGameMode_Arena::OnTimeLimit()
{
	// SetEndCams: more rounds wins, else a draw
	const AMFGameState* GS = GetGameState<AMFGameState>();
	if (!GS || GS->TeamScores.Num() < 2)
	{
		return;
	}
	const int32 A = GS->TeamScores[0], B = GS->TeamScores[1];
	FinishMatch(A == B ? FString(TEXT("DRAW")) : FString::Printf(TEXT("%s Team Wins!"), *GS->GetTeamName(A > B ? 0 : 1).ToString()));
}
