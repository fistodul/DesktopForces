#include "Game/MFGameMode.h"
#include "Game/MFGameState.h"
#include "Game/MFPlayerState.h"
#include "Game/MFPlayerStart.h"
#include "Player/MFCharacter.h"
#include "Player/MFPlayerController.h"
#include "Loadout/MFLoadout.h"
#include "UI/MFHUD.h"
#include "MobileForces.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AMFGameMode::AMFGameMode()
{
	DefaultPawnClass = AMFCharacter::StaticClass();
	PlayerControllerClass = AMFPlayerController::StaticClass();
	PlayerStateClass = AMFPlayerState::StaticClass();
	GameStateClass = AMFGameState::StaticClass();
	HUDClass = AMFHUD::StaticClass();
	bUseSeamlessTravel = false;
	bStartPlayersAsSpectators = false;
}

void AMFGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	FragLimit = UGameplayStatics::GetIntOption(Options, TEXT("FragLimit"), FragLimit);
	TimeLimit = UGameplayStatics::GetIntOption(Options, TEXT("TimeLimit"), TimeLimit);
	RespawnWait = UGameplayStatics::GetIntOption(Options, TEXT("RespawnWait"), RespawnWait);
}

void AMFGameMode::InitGameState()
{
	Super::InitGameState();
	if (AMFGameState* GS = GetGameState<AMFGameState>())
	{
		GS->bTeamGame = IsTeamGame();
		GS->FragLimit = FragLimit;
		GS->TimeLimit = TimeLimit;
		GS->RemainingTime = TimeLimit * 60;
		GS->RespawnWait = FMath::Max(1, RespawnWait);
		GS->GameName = GameName;
	}
}

void AMFGameMode::StartPlay()
{
	Super::StartPlay();
	GetWorldTimerManager().SetTimer(ClockTimer, this, &AMFGameMode::ClockTick, 1.f, true);
}

void AMFGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
}

void AMFGameMode::ClockTick()
{
	AMFGameState* GS = GetGameState<AMFGameState>();
	if (!GS || GS->bMatchOver || TimeLimit <= 0)
	{
		return;
	}
	GS->RemainingTime = FMath::Max(0, GS->RemainingTime - 1);
	if (GS->RemainingTime == 0)
	{
		OnTimeLimit();
	}
}

void AMFGameMode::OnTimeLimit()
{
	AMFGameState* GS = GetGameState<AMFGameState>();
	TArray<AMFPlayerState*> Sorted = GS->GetSortedPlayers();
	EndMatch(Sorted.Num() > 0 ? FString::Printf(TEXT("%s Wins!"), *Sorted[0]->GetPlayerName()) : TEXT("Time up"));
}

// --- spawning ----------------------------------------------------------------------

bool AMFGameMode::IsStartAllowed(const AMFPlayerStart* Start, AController* Player) const
{
	return Start->bEnabled && Start->IsNeutral();
}

AActor* AMFGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// RageDeathMatch.FindPlayerStart: random score, avoid the last spot, avoid other players
	TArray<AMFPlayerStart*> Candidates;
	TArray<AMFPlayerStart*> All;
	for (TActorIterator<AMFPlayerStart> It(GetWorld()); It; ++It)
	{
		All.Add(*It);
		if (IsStartAllowed(*It, Player))
		{
			Candidates.Add(*It);
		}
	}
	if (Candidates.Num() == 0)
	{
		Candidates = All;
	}
	if (Candidates.Num() == 0)
	{
		return Super::ChoosePlayerStart_Implementation(Player);   // plain APlayerStart (test maps)
	}

	AActor* Best = nullptr;
	float BestScore = -FLT_MAX;
	for (AMFPlayerStart* Start : Candidates)
	{
		float Score = Start == LastStartSpot.Get() ? -10000.f : 3000.f * FMath::FRand();
		for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
		{
			const AMFCharacter* Other = *It;
			if (!Other->IsAlive() || Other->GetController() == Player)
			{
				continue;
			}
			const float Dist = FVector::Dist(Other->GetActorLocation(), Start->GetActorLocation());
			if (Dist < 126.f)                      // MF: CollisionRadius + CollisionHeight (56 uu)
			{
				Score -= 1000000.f;
			}
			else if (Dist < 4500.f)                // MF: 2000 uu
			{
				Score -= 22500.f - Dist;           // MF: 10000 - dist (in uu)
			}
		}
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Start;
		}
	}
	LastStartSpot = Best;
	return Best;
}

bool AMFGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
	const AMFGameState* GS = GetGameState<AMFGameState>();
	return (!GS || !GS->bMatchOver) && Super::PlayerCanRestart_Implementation(Player);
}

void AMFGameMode::RestartPlayer(AController* NewPlayer)
{
	RespawnTimers.Remove(NewPlayer);
	Super::RestartPlayer(NewPlayer);
}

void AMFGameMode::SetPlayerDefaults(APawn* PlayerPawn)
{
	Super::SetPlayerDefaults(PlayerPawn);
	AMFCharacter* C = Cast<AMFCharacter>(PlayerPawn);
	if (!C)
	{
		return;
	}
	for (TSubclassOf<AMFWeapon> W : C->DefaultInventory)
	{
		C->GiveWeapon(W, true);
	}
	// Spawning next to a loadout crate / inside a loadout zone opens the loadout screen
	if (CanUseLoadout(C))
	{
		if (AMFPlayerController* PC = Cast<AMFPlayerController>(C->GetController()))
		{
			PC->EnterLoadout();
		}
	}
}

bool AMFGameMode::CanUseLoadout(AMFCharacter* Character) const
{
	return UMFLoadoutLibrary::FindLoadoutSourceFor(Character) != nullptr;
}

// --- damage & kills ----------------------------------------------------------------

float AMFGameMode::ModifyDamage(float Damage, AMFCharacter* Victim, AController* Instigator) const
{
	if (!Instigator)
	{
		return Damage;
	}
	return bHardCoreMode ? Damage * 1.5f : Damage;
}

void AMFGameMode::Killed(AController* Killer, AController* Victim, AMFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType)
{
	AMFPlayerState* KillerPS = Killer ? Killer->GetPlayerState<AMFPlayerState>() : nullptr;
	AMFPlayerState* VictimPS = Victim ? Victim->GetPlayerState<AMFPlayerState>() : (VictimPawn ? VictimPawn->GetPlayerState<AMFPlayerState>() : nullptr);
	if (VictimPS)
	{
		VictimPS->DeathTime = GetGameState<AMFGameState>()->GetServerWorldTimeSeconds();
	}
	ScoreKill(KillerPS, VictimPS);

	if (AMFGameState* GS = GetGameState<AMFGameState>())
	{
		const UMFDamageType* MFType = DamageType ? Cast<UMFDamageType>(DamageType->GetDefaultObject()) : nullptr;
		const FString Verb = MFType ? MFType->DeathVerb.ToString() : TEXT("killed");
		const FString VictimName = VictimPS ? VictimPS->GetPlayerName() : TEXT("Someone");
		if (KillerPS && KillerPS != VictimPS)
		{
			GS->MulticastKill(KillerPS->GetPlayerName(), VictimName, Verb);
			if (!bFirstBlood)
			{
				bFirstBlood = true;
				GS->MulticastAnnounce(FString::Printf(TEXT("%s drew first blood!"), *KillerPS->GetPlayerName()));
			}
			// Killing sprees (RageDeathMatch.NotifySpree, RPlayer.spreenote)
			static const TCHAR* Sprees[] = { TEXT("is on a killing spree!"), TEXT("is on a rampage!"), TEXT("is dominating!"),
				TEXT("is brutalizing the competition!"), TEXT("is unstoppable!"), TEXT("owns you!") };
			if (KillerPS->Spree >= 5 && KillerPS->Spree % 5 == 0)
			{
				const int32 Idx = FMath::Min(KillerPS->Spree / 5 - 1, 5);
				GS->MulticastAnnounce(FString::Printf(TEXT("%s %s"), *KillerPS->GetPlayerName(), Sprees[Idx]));
			}
		}
		else
		{
			GS->MulticastKill(FString(), VictimName, TEXT("died"));
		}
	}

	CheckEndConditions(KillerPS);
	if (Victim)
	{
		ScheduleRespawn(Victim);
	}
}

void AMFGameMode::ScoreKill(AMFPlayerState* Killer, AMFPlayerState* Victim)
{
	if (Victim)
	{
		Victim->Deaths++;
		Victim->Spree = 0;
	}
	if (!Killer || Killer == Victim)
	{
		if (Victim)
		{
			Victim->SetScore(Victim->GetScore() - 1.f);
		}
		return;
	}
	Killer->Kills++;
	Killer->Spree++;
	Killer->SetScore(Killer->GetScore() + 1.f);
}

void AMFGameMode::CheckEndConditions(AMFPlayerState* LastKiller)
{
	if (FragLimit > 0 && LastKiller && LastKiller->GetScore() >= FragLimit)
	{
		EndMatch(FString::Printf(TEXT("%s Wins!"), *LastKiller->GetPlayerName()));
	}
}

void AMFGameMode::ScheduleRespawn(AController* Victim)
{
	const AMFGameState* GS = GetGameState<AMFGameState>();
	if (GS && GS->bMatchOver)
	{
		return;
	}
	FTimerHandle& H = RespawnTimers.FindOrAdd(Victim);
	TWeakObjectPtr<AController> Weak(Victim);
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, Weak]()
	{
		if (AController* C = Weak.Get(); C && !C->GetPawn())
		{
			RestartPlayer(C);
		}
	}), float(FMath::Max(1, RespawnWait)), false);
}

void AMFGameMode::EndMatch(const FString& WinnerText)
{
	AMFGameState* GS = GetGameState<AMFGameState>();
	if (!GS || GS->bMatchOver)
	{
		return;
	}
	GS->bMatchOver = true;
	GS->WinnerText = WinnerText;
	GS->MulticastAnnounce(WinnerText);
	for (auto& Pair : RespawnTimers)
	{
		GetWorldTimerManager().ClearTimer(Pair.Value);
	}
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
	{
		It->StopFire(false);
		It->StopFire(true);
		It->GetCharacterMovement()->DisableMovement();
	}
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, this, &AMFGameMode::RestartMap, float(RestartWait), false);
}

void AMFGameMode::RestartMap()
{
	GetWorld()->ServerTravel(TEXT("?Restart"), false);
}

// ---------------------------------------------------------------------------------------
// Team deathmatch
// ---------------------------------------------------------------------------------------

AMFGameMode_TDM::AMFGameMode_TDM()
{
	GameName = TEXT("Team DM");
	FragLimit = 0;
	TimeLimit = 12;
}

void AMFGameMode_TDM::InitGameState()
{
	Super::InitGameState();
	if (AMFGameState* GS = GetGameState<AMFGameState>())
	{
		GS->GoalTeamScore = GoalTeamScore;
	}
}

EMFTeam AMFGameMode_TDM::PickTeam() const
{
	int32 Count[2] = { 0, 0 };
	if (const AMFGameState* GS = GetGameState<AMFGameState>())
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			if (const AMFPlayerState* M = Cast<AMFPlayerState>(PS); M && M->GetTeam() != EMFTeam::None)
			{
				Count[uint8(M->GetTeam())]++;
			}
		}
	}
	return Count[0] <= Count[1] ? EMFTeam::Blue : EMFTeam::Red;
}

void AMFGameMode_TDM::PostLogin(APlayerController* NewPlayer)
{
	if (AMFPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<AMFPlayerState>() : nullptr)
	{
		PS->SetTeam(PickTeam());
	}
	Super::PostLogin(NewPlayer);
}

bool AMFGameMode_TDM::IsStartAllowed(const AMFPlayerStart* Start, AController* Player) const
{
	const AMFPlayerState* PS = Player ? Player->GetPlayerState<AMFPlayerState>() : nullptr;
	return Start->bEnabled && PS && Start->TeamNumber == uint8(PS->GetTeam());
}

float AMFGameMode_TDM::ModifyDamage(float Damage, AMFCharacter* Victim, AController* Instigator) const
{
	Damage = Super::ModifyDamage(Damage, Victim, Instigator);
	const AMFPlayerState* InstPS = Instigator ? Instigator->GetPlayerState<AMFPlayerState>() : nullptr;
	if (InstPS && Victim && Victim->GetController() != Instigator && InstPS->GetTeam() == Victim->GetTeam())
	{
		return Damage * FriendlyFireScale;
	}
	return Damage;
}

void AMFGameMode_TDM::ScoreKill(AMFPlayerState* Killer, AMFPlayerState* Victim)
{
	AMFGameState* GS = GetGameState<AMFGameState>();
	const bool bTeamKill = Killer && Victim && Killer != Victim && Killer->GetTeam() == Victim->GetTeam();
	if (!bTeamKill)
	{
		Super::ScoreKill(Killer, Victim);
	}
	else if (Victim)
	{
		Victim->Deaths++;
	}
	// RageTeamGame.ScoreKill (bScoreTeamKills)
	if (!GS || !Victim || Victim->GetTeam() == EMFTeam::None)
	{
		return;
	}
	if (!Killer || Killer == Victim)
	{
		GS->TeamScores[uint8(Victim->GetTeam())] -= 1;
	}
	else if (!bTeamKill)
	{
		GS->TeamScores[uint8(Killer->GetTeam())] += 1;
	}
	else if (FriendlyFireScale > 0.f)
	{
		GS->TeamScores[uint8(Victim->GetTeam())] -= 1;
		Killer->SetScore(Killer->GetScore() - 1.f);
	}
}

void AMFGameMode_TDM::CheckEndConditions(AMFPlayerState* LastKiller)
{
	const AMFGameState* GS = GetGameState<AMFGameState>();
	if (!GS || GoalTeamScore <= 0)
	{
		return;
	}
	for (int32 T = 0; T < 2; ++T)
	{
		if (GS->TeamScores[T] >= GoalTeamScore)
		{
			EndMatch(T == 0 ? TEXT("Blue Team Wins!") : TEXT("Red Team Wins!"));
			return;
		}
	}
}

void AMFGameMode_TDM::OnTimeLimit()
{
	const AMFGameState* GS = GetGameState<AMFGameState>();
	const int32 B = GS->TeamScores[0], R = GS->TeamScores[1];
	EndMatch(B == R ? TEXT("DRAW - Neither team wins") : (B > R ? TEXT("Blue Team Wins!") : TEXT("Red Team Wins!")));
}
