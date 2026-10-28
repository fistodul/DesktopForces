#include "Game/Arena/Arena.h"
#include "Game/DFGameState.h"
#include "Game/DFPlayerState.h"
#include "Player/DFCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

AArena::AArena() {
	GameName = TEXT("Arena");
	MFGameClass = TEXT("arenaMode");
	NumTeams = 2;
	ScoreLimit = 5; // arenaMode: rounds to win
	TimeLimit = 10;
	bScoreTeamKills = false;
}

void AArena::HandleMatchHasStarted() {
	UnlockLeft = TimeUntilUnlock;
	Super::HandleMatchHasStarted();
}

int32 AArena::AliveCount(uint8 Team) const {
	int32 N = 0;
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It)
		N += It->IsAlive() && It->GetTeam() == Team;
	return N;
}

void AArena::RestartPlayer(AController* NewPlayer) {
	// out for the rest of the round (MF: playerWaiting until doRestartRound)
	if (bDisableRespawn && !bRestartingRound && DeadThisRound.Contains(NewPlayer))
		return;
	Super::RestartPlayer(NewPlayer);
}

void AArena::Killed(AController* Killer, AController* Victim, ADFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType) {
	Super::Killed(Killer, Victim, VictimPawn, DamageType);
	if (Victim)
		DeadThisRound.Add(Victim);
	const uint8 Team = VictimPawn ? VictimPawn->GetTeam() : DF::NoTeam;
	if (bRoundOver || Team > 1 || AliveCount(Team) > 0)
		return;
	// the last of their team: the round to the others
	const ADFPlayerState* KPS = Killer ? Killer->GetPlayerState<ADFPlayerState>() : nullptr;
	RoundEnd(KPS && KPS->GetTeam() <= 1 && KPS->GetTeam() != Team ? KPS->GetTeam() : uint8(1 - Team), false);
}

void AArena::Logout(AController* Exiting) {
	DeadThisRound.Remove(Exiting);
	Super::Logout(Exiting);
}

void AArena::UpdatePoints() {
	ADFGameState* GS = GetGameState<ADFGameState>();
	if (!GS || !IsMatchInProgress() || bRoundOver || Points.Num() == 0)
		return;
	AHoldoutPoint* P = Points[0];
	// arenaModeTimer.Timer: locked for TimeUntilUnlock seconds, counted down at the end
	if (UnlockLeft > 0) {
		--UnlockLeft;
		if (UnlockLeft == 10)
			Event(TEXT("Get ready"), DF::NoTeam);
		else if (UnlockLeft > 0 && UnlockLeft <= 5)
			Event(FString::FromInt(UnlockLeft), DF::NoTeam);
		else if (UnlockLeft == 0)
			Event(TEXT("Point unlocked"), DF::NoTeam);
		P->SetOwnerTeam(DF::NoTeam);
		return;
	}
	// who's on it (CollisionRadius / Height 60 uu)
	bool bOn[2] = { false, false };
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It) {
		const FVector D = It->GetActorLocation() - P->GetActorLocation();
		if (It->IsAlive() && It->GetTeam() <= 1 && FVector2D(D).Size() < 135.f + 40.f && FMath::Abs(D.Z) < 135.f + 90.f)
			bOn[It->GetTeam()] = true;
	}
	if (CapturingTeam <= 1 && !bOn[CapturingTeam]) {
		CapturingTeam = DF::NoTeam; // unTouch: control lapses, the count starts over
		CaptureProgress = 0;
	}
	for (uint8 T = 0; T < 2; ++T) {
		if (bOn[T] && !bOn[1 - T] && CapturingTeam != T) {
			CapturingTeam = T; // touch by the other team: theirs now, from zero
			CaptureProgress = 0;
			if (GetWorld()->GetTimeSeconds() - LastAnnounce > 5.f) {
				LastAnnounce = GetWorld()->GetTimeSeconds();
				Event(FString::Printf(TEXT("%s team is taking the point"), *GS->GetTeamName(T).ToString()), T);
			}
		}
	}
	if (CapturingTeam <= 1 && !bOn[1 - CapturingTeam])
		++CaptureProgress; // UpdateTimer: contested points don't count
	P->SetOwnerTeam(CapturingTeam);
	for (int32 T = 0; T < P->TeamTimes.Num(); ++T)
		P->TeamTimes[T] = T == CapturingTeam ? CaptureProgress : 0;
	if (CapturingTeam <= 1 && CaptureProgress >= TimeUntilCapture)
		RoundEnd(CapturingTeam, true); // TimerLimit
}

void AArena::RoundEnd(uint8 Winner, bool bCaptured) {
	ADFGameState* GS = GetGameState<ADFGameState>();
	if (!GS || !GS->TeamScores.IsValidIndex(Winner) || bRoundOver)
		return;
	bRoundOver = true;
	GS->TeamScores[Winner] += 1;
	const FString Team = GS->GetTeamName(Winner).ToString();
	if (ScoreLimit > 0 && GS->TeamScores[Winner] >= ScoreLimit) {
		FinishMatch(FString::Printf(TEXT("%s Team Wins!"), *Team));
		return;
	}
	Event(bCaptured ? FString::Printf(TEXT("%s team captured the point!"), *Team) : FString::Printf(TEXT("%s team wins the round!"), *Team), Winner);
	GetWorldTimerManager().SetTimer(RoundTimer, this, &AArena::NextRound, 3.f, false);
}

void AArena::NextRound() {
	// doRestartRound: everyone back, the point locked again
	DeadThisRound.Reset();
	CapturingTeam = DF::NoTeam;
	CaptureProgress = 0;
	UnlockLeft = TimeUntilUnlock;
	bRoundOver = false;
	TGuardValue<bool> Restarting(bRestartingRound, true);
	RestartRound();
}

void AArena::OnTimeLimit() {
	// SetEndCams: more rounds wins, else a draw
	const ADFGameState* GS = GetGameState<ADFGameState>();
	if (!GS || GS->TeamScores.Num() < 2)
		return;
	const int32 A = GS->TeamScores[0], B = GS->TeamScores[1];
	FinishMatch(A == B ? FString(TEXT("DRAW")) : FString::Printf(TEXT("%s Team Wins!"), *GS->GetTeamName(A > B ? 0 : 1).ToString()));
}
