#include "Game/MFGameMode.h"
#include "Vehicles/MFVehicle.h"
#include "Weapons/MFTripBomb.h"
#include "Weapons/MFProjectile.h"
#include "Weapons/MFWeapon.h"
#include "Player/MFCharacter.h"
#include "EngineUtils.h"
#include "MobileForces.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/StaticMeshActor.h"
#include "Game/MFGameState.h"
#include "Game/MFMessages.h"
#include "Game/MFPlayerState.h"
#include "Game/MFPlayerStart.h"
#include "AI/MFBotController.h"
#include "Core/MFGameSettings.h"
#include "Loadout/MFLoadout.h"
#include "Player/MFCharacter.h"
#include "Player/MFPlayerController.h"
#include "UI/MFHUD.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AMFGameMode::AMFGameMode()
{
	DefaultPawnClass = AMFCharacter::StaticClass();
	PlayerControllerClass = AMFPlayerController::StaticClass();
	PlayerStateClass = AMFPlayerState::StaticClass();
	GameStateClass = AMFGameState::StaticClass();
	HUDClass = AMFHUD::StaticClass();
	bDelayedStart = false;
	bUseSeamlessTravel = false;
}

void AMFGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	EnsureKillZ();
	NumTeams = FMath::Max(0, UGameplayStatics::GetIntOption(Options, TEXT("Teams"), NumTeams));
	FragLimit = UGameplayStatics::GetIntOption(Options, TEXT("FragLimit"), FragLimit);
	TimeLimit = UGameplayStatics::GetIntOption(Options, TEXT("TimeLimit"), TimeLimit);
	TimeLimitSeconds = UGameplayStatics::GetIntOption(Options, TEXT("TimeLimitSecs"), TimeLimitSeconds);
	GoalTeamScore = UGameplayStatics::GetIntOption(Options, TEXT("GoalTeamScore"), GoalTeamScore);
	RespawnWait = UGameplayStatics::GetIntOption(Options, TEXT("RespawnWait"), RespawnWait);
	BotCount = UGameplayStatics::GetIntOption(Options, TEXT("Bots"), BotCount);
	bVehicles = UGameplayStatics::GetIntOption(Options, TEXT("Vehicles"), bVehicles ? 1 : 0) != 0;
	const FString Skill = UGameplayStatics::ParseOption(Options, TEXT("BotSkill"));
	if (!Skill.IsEmpty())
	{
		BotSkill = FMath::Clamp(FCString::Atof(*Skill), 0.f, 1.f);
	}
}

void AMFGameMode::InitGameState()
{
	Super::InitGameState();
	if (AMFGameState* GS = GetGameState<AMFGameState>())
	{
		GS->NumTeams = NumTeams;
		GS->TeamScores.Init(0, NumTeams);
		GS->FragLimit = FragLimit;
		GS->TimeLimit = TimeLimit;
		GS->TimeLimitSeconds = TimeLimitSeconds;
		GS->GoalTeamScore = GoalTeamScore;
		GS->RespawnWait = FMath::Max(1, RespawnWait);
		GS->GameName = GameName;
	}
}

void AMFGameMode::PostLogin(APlayerController* NewPlayer)
{
	if (AMFPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<AMFPlayerState>() : nullptr)
	{
		PS->SetTeam(IsTeamGame() ? PickTeam() : MF::NoTeam);
	}
	Super::PostLogin(NewPlayer);
}

void AMFGameMode::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();
	AddBots(BotCount);
	if (bVehicles)
	{
		AMFVehicleGenerator::SpawnAll(GetWorld());   // the map's Buggy/Humvee/Truck/ArmadilloGenerators
	}
	GetWorldTimerManager().SetTimer(TimeLimitTimer, this, &AMFGameMode::CheckTimeLimit, 1.f, true);
}

void AMFGameMode::CheckTimeLimit()
{
	const AMFGameState* GS = GetGameState<AMFGameState>();
	if (GS && IsMatchInProgress() && GS->GetRemainingTime() == 0)
	{
		OnTimeLimit();
	}
}

uint8 AMFGameMode::PickTeam(uint8 Preferred) const
{
	TArray<int32> Count;
	Count.Init(0, NumTeams);
	if (const AMFGameState* GS = GetGameState<AMFGameState>())
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			if (const AMFPlayerState* M = Cast<AMFPlayerState>(PS); M && Count.IsValidIndex(M->GetTeam()))
			{
				Count[M->GetTeam()]++;
			}
		}
	}
	int32 Best = Count.IsValidIndex(Preferred) ? Preferred : 0;
	for (int32 T = 0; T < Count.Num(); ++T)
	{
		if (Count[T] < Count[Best])
		{
			Best = T;
		}
	}
	return uint8(Best);
}

// --- spawning ----------------------------------------------------------------------

bool AMFGameMode::IsStartAllowed(const AMFPlayerStart* Start, AController* Player) const
{
	if (!Start->bEnabled || Start->bCaptainStart)
	{
		return false;   // captain starts: see AMFGameMode_Captains
	}
	if (!IsTeamGame())
	{
		return Start->IsNeutral();
	}
	const AMFPlayerState* PS = Player ? Player->GetPlayerState<AMFPlayerState>() : nullptr;
	return PS && Start->TeamNumber == PS->GetTeam();
}

AActor* AMFGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// RageDeathMatch.FindPlayerStart: random score, avoid the last spot, avoid other players
	TArray<AMFPlayerStart*> Candidates, All;
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
		Candidates = All;   // e.g. team 3 on a map built for 2 teams
	}
	if (Candidates.Num() == 0)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}
	AActor* Best = nullptr;
	float BestScore = -FLT_MAX;
	for (AMFPlayerStart* Start : Candidates)
	{
		float Score = Start == LastStartSpot.Get() ? -10000.f : 3000.f * FMath::FRand();
		for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->IsAlive() || It->GetController() == Player)
			{
				continue;
			}
			const float Dist = FVector::Dist(It->GetActorLocation(), Start->GetActorLocation());
			if (Dist < 126.f)               // MF: CollisionRadius + CollisionHeight (56 uu)
			{
				Score -= 1000000.f;
			}
			else if (Dist < 4500.f)         // MF: 2000 uu
			{
				Score -= 22500.f - Dist;
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
	return IsMatchInProgress() && Super::PlayerCanRestart_Implementation(Player);
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
	if (CanUseLoadout(C))
	{
		if (AMFPlayerController* PC = Cast<AMFPlayerController>(C->GetController()))
		{
			C->BeginStartupLoadout();
			PC->EnterLoadout();
		}
	}
}

void AMFGameMode::EnsureKillZ()
{
	// Maps built before KillZ was set (engine default: never) - 20 m under the lowest geometry
	AWorldSettings* WS = GetWorldSettings();
	if (!WS || WS->KillZ > -1.0e6f)
	{
		return;
	}
	float MinZ = TNumericLimits<float>::Max();
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		FVector Origin, Extent;
		It->GetActorBounds(false, Origin, Extent);
		MinZ = FMath::Min(MinZ, float(Origin.Z - Extent.Z));
	}
	if (MinZ < TNumericLimits<float>::Max())
	{
		WS->bEnableWorldBoundsChecks = true;
		WS->KillZ = MinZ - 2000.f;
	}
}

bool AMFGameMode::CanUseLoadout(AMFCharacter* Character) const
{
	return UMFLoadoutLibrary::FindLoadoutSourceFor(Character) != nullptr;
}

void AMFGameMode::AddBots(int32 Count, float Skill)
{
	const TArray<FMFBotDef>& Roster = UMFGameSettings::Get().Bots;
	for (int32 i = 0; i < Count; ++i)
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMFBotController* Bot = GetWorld()->SpawnActor<AMFBotController>(SP);
		if (!Bot)
		{
			continue;
		}
		// Roster in order; past its end, recycle profiles with numbered names (no cap)
		FMFBotDef Def = Roster.Num() > 0 ? Roster[BotsAdded % Roster.Num()] : FMFBotDef();
		if (Roster.Num() == 0 || BotsAdded >= Roster.Num())
		{
			Def.Name = FString::Printf(TEXT("%s%d"), Def.Name.IsEmpty() ? TEXT("Bot") : *Def.Name, BotsAdded / FMath::Max(1, Roster.Num()) + 1);
		}
		++BotsAdded;
		Bot->Profile = Def;
		Bot->Skill = Skill >= 0.f ? Skill : BotSkill;
		if (AMFPlayerState* PS = Bot->GetPlayerState<AMFPlayerState>())
		{
			PS->SetPlayerName(Def.Name);
			PS->SetIsABot(true);
			PS->SetTeam(IsTeamGame() ? PickTeam(Def.PreferredTeam) : MF::NoTeam);
		}
		++NumBots;
		RestartPlayer(Bot);
	}
}

// --- damage & kills ----------------------------------------------------------------

float AMFGameMode::ModifyDamage(float Damage, AMFCharacter* Victim, AController* Instigator) const
{
	if (!Instigator)
	{
		return Damage;
	}
	if (bHardCoreMode)
	{
		Damage *= 1.5f;
	}
	const AMFPlayerState* InstPS = Instigator->GetPlayerState<AMFPlayerState>();
	if (IsTeamGame() && InstPS && Victim && Victim->GetController() != Instigator && InstPS->GetTeam() == Victim->GetTeam())
	{
		Damage *= FriendlyFireScale;
	}
	return Damage;
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

	BroadcastLocalized(this, UMFDeathMessage::StaticClass(), 0, KillerPS, VictimPS, DamageType.Get());
	if (KillerPS && KillerPS != VictimPS)
	{
		if (!bFirstBlood)
		{
			bFirstBlood = true;
			BroadcastLocalized(this, UMFAnnouncement::StaticClass(), UMFAnnouncement::FirstBlood, KillerPS);
		}
		if (KillerPS->Spree >= 5 && KillerPS->Spree % 5 == 0)
		{
			BroadcastLocalized(this, UMFAnnouncement::StaticClass(), UMFAnnouncement::Spree + FMath::Min(KillerPS->Spree / 5 - 1, 5), KillerPS);
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
	AMFGameState* GS = GetGameState<AMFGameState>();
	const bool bSuicide = !Killer || Killer == Victim;
	const bool bTeamKill = !bSuicide && IsTeamGame() && Victim && Killer->GetTeam() == Victim->GetTeam();
	if (Victim)
	{
		Victim->Deaths++;
		Victim->Spree = 0;
	}
	if (bSuicide)
	{
		if (Victim)
		{
			Victim->SetScore(Victim->GetScore() - 1.f);   // GameInfo.ScoreKill
		}
	}
	else if (!bTeamKill)
	{
		Killer->Kills++;
		Killer->Spree++;
		Killer->SetScore(Killer->GetScore() + 1.f);
	}
	// Team scores (RageTeamGame.ScoreKill, bScoreTeamKills)
	if (!bScoreTeamKills || !GS || !Victim || !GS->TeamScores.IsValidIndex(Victim->GetTeam()))
	{
		return;
	}
	if (bSuicide)
	{
		GS->TeamScores[Victim->GetTeam()] -= 1;
	}
	else if (!bTeamKill)
	{
		if (GS->TeamScores.IsValidIndex(Killer->GetTeam()))
		{
			GS->TeamScores[Killer->GetTeam()] += 1;
		}
	}
	else if (FriendlyFireScale > 0.f)
	{
		GS->TeamScores[Victim->GetTeam()] -= 1;
		Killer->SetScore(Killer->GetScore() - 1.f);
	}
}

void AMFGameMode::CheckEndConditions(AMFPlayerState* LastKiller)
{
	const AMFGameState* GS = GetGameState<AMFGameState>();
	if (IsTeamGame())
	{
		if (GS && GoalTeamScore > 0)
		{
			for (int32 T = 0; T < GS->TeamScores.Num(); ++T)
			{
				if (GS->TeamScores[T] >= GoalTeamScore)
				{
					FinishMatch(FString::Printf(TEXT("%s Team Wins!"), *UMFGameSettings::Get().GetTeamName(T).ToString()));
					return;
				}
			}
		}
	}
	else if (FragLimit > 0 && LastKiller && LastKiller->GetScore() >= FragLimit)
	{
		FinishMatch(FString::Printf(TEXT("%s Wins!"), *LastKiller->GetPlayerName()));
	}
}

void AMFGameMode::OnTimeLimit()
{
	const AMFGameState* GS = GetGameState<AMFGameState>();
	if (IsTeamGame())
	{
		int32 Best = 0;
		bool bTie = false;
		for (int32 T = 1; T < GS->TeamScores.Num(); ++T)
		{
			if (GS->TeamScores[T] > GS->TeamScores[Best]) { Best = T; bTie = false; }
			else if (GS->TeamScores[T] == GS->TeamScores[Best]) { bTie = true; }
		}
		FinishMatch(bTie ? TEXT("DRAW") : FString::Printf(TEXT("%s Team Wins!"), *UMFGameSettings::Get().GetTeamName(Best).ToString()));
		return;
	}
	const TArray<AMFPlayerState*> Sorted = GS->GetSortedPlayers();
	FinishMatch(Sorted.Num() > 0 ? FString::Printf(TEXT("%s Wins!"), *Sorted[0]->GetPlayerName()) : TEXT("Time up"));
}

void AMFGameMode::ScheduleRespawn(AController* Victim)
{
	if (!IsMatchInProgress())
	{
		return;
	}
	FTimerHandle& H = RespawnTimers.FindOrAdd(Victim);
	TWeakObjectPtr<AController> Weak(Victim);
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, Weak]()
	{
		if (AController* C = Weak.Get(); C && !C->GetPawn() && IsMatchInProgress())
		{
			RestartPlayer(C);
		}
	}), float(FMath::Max(1, RespawnWait)), false);
}

void AMFGameMode::BroadcastChat(AController* Sender, const FString& Msg, bool bTeam)
{
	APlayerState* SenderPS = Sender ? Sender->PlayerState : nullptr;
	const AMFPlayerState* MFS = Cast<AMFPlayerState>(SenderPS);
	UE_LOG(LogMF, Display, TEXT("%s%s%s"), bTeam ? TEXT("[team] ") : TEXT(""), SenderPS ? *(SenderPS->GetPlayerName() + TEXT(": ")) : TEXT(""), *Msg);
	const bool bTeamOnly = bTeam && IsTeamGame() && MFS && MFS->HasTeam();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		const AMFPlayerState* PS = PC ? PC->GetPlayerState<AMFPlayerState>() : nullptr;
		if (PC && (!bTeamOnly || (PS && PS->GetTeam() == MFS->GetTeam())))
		{
			PC->ClientTeamMessage(SenderPS, Msg, bTeamOnly ? FName(TEXT("TeamSay")) : FName(TEXT("Say")));
		}
	}
}

void AMFGameMode::Say(const FString& Msg)
{
	BroadcastChat(GetWorld()->GetFirstPlayerController(), Msg.Left(128), false);
}

FString AMFGameMode::GetOrderAbbrev(int32 Order)
{
	// Rage.int OrderAbbrev / OrderString 0-4 (the menu)
	static const TCHAR* Abbrev[] = { TEXT("Defend!"), TEXT("Defend this area."), TEXT("Attack!"), TEXT("Cover me."), TEXT("Improvise!") };
	return Order >= 0 && Order < 5 ? Abbrev[Order] : FString();
}

FString AMFGameMode::GetOrderString(int32 Order) const
{
	static const TCHAR* Lines[] = { TEXT("Defend the command post."), TEXT("Defend this area."),
		TEXT("Attack the enemy base."), TEXT("Cover me."), TEXT("Improvise, you're on your own.") };
	if (Order == 4 && FMath::FRand() < 0.4f)
	{
		return TEXT("Fire at will.");   // RageVoicePack.SetOrderMessage: OrderString[15] 40% of the time
	}
	return Order >= 0 && Order < 5 ? Lines[Order] : FString();
}

void AMFGameMode::GiveOrder(AController* Giver, int32 Order, APlayerState* Target)
{
	const AMFPlayerState* GiverPS = Giver ? Giver->GetPlayerState<AMFPlayerState>() : nullptr;
	if (!GiverPS)
	{
		return;
	}
	// MF chat line: "Sender: Heywood, Defend this area." (to the team)
	const FString Line = GetOrderString(Order);
	BroadcastChat(Giver, Target ? FString::Printf(TEXT("%s, %s"), *Target->GetPlayerName(), *Line) : Line, true);
	static const TCHAR* Acks[] = { TEXT("Acknowledged"), TEXT("Roger that"), TEXT("Moving out"), TEXT("I'm on my way") };
	for (TActorIterator<AMFBotController> It(GetWorld()); It; ++It)
	{
		const AMFPlayerState* PS = It->GetPlayerState<AMFPlayerState>();
		if (!PS || PS == GiverPS || (IsTeamGame() && PS->GetTeam() != GiverPS->GetTeam()) || (Target && PS != Target))
		{
			continue;
		}
		It->SetOrder(static_cast<EMFBotOrder>(Order), Giver->GetPawn());
		// RageVoicePack ack (Rage.int AckString), from the bot to its team, a moment later
		TWeakObjectPtr<AMFBotController> Bot = *It;
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, Bot]()
		{
			if (Bot.IsValid())
			{
				BroadcastChat(Bot.Get(), Acks[FMath::RandRange(0, 3)], true);
			}
		}), FMath::FRandRange(0.6f, 1.5f), false);
	}
}

void AMFGameMode::FinishMatch(const FString& Winner)
{
	if (!IsMatchInProgress())
	{
		return;
	}
	if (AMFGameState* GS = GetGameState<AMFGameState>())
	{
		GS->WinnerText = Winner;
	}
	EndMatch();
}

void AMFGameMode::HandleMatchHasEnded()
{
	Super::HandleMatchHasEnded();
	GetWorldTimerManager().ClearTimer(TimeLimitTimer);
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
	BroadcastLocalized(this, UMFAnnouncement::StaticClass(), UMFAnnouncement::MatchEnd);
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, this, &AMFGameMode::RestartGame, float(RestartWait), false);
}

AMFGameMode_TDM::AMFGameMode_TDM()
{
	GameName = TEXT("Team DM");
	NumTeams = 2;
	FragLimit = 0;
	TimeLimit = 12;
}

void AMFGameMode::RestartRound()
{
	if (AMFGameState* GS = GetGameState<AMFGameState>())
	{
		GS->RoundStartTime = GS->ElapsedTime;
	}
	// what the last round left lying around: trip bombs, mines, thrown items, dropped weapons
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->IsA<AMFTripBomb>() || It->IsA<AMFProjectile>() || (It->IsA<AMFWeapon>() && Cast<AMFWeapon>(*It)->IsDropped()))
		{
			It->Destroy();
		}
	}
	// everyone starts fresh (MF RestartRound: discard inventory, StartupInLoadout)
	for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
	{
		AController* C = It->Get();
		if (!C || !C->PlayerState || C->PlayerState->IsSpectator())
		{
			continue;
		}
		if (APawn* P = C->GetPawn())
		{
			if (AMFCharacter* MC = Cast<AMFCharacter>(P))
			{
				MC->ClearInventory(false);
			}
			P->Destroy();
		}
		RestartPlayer(C);
	}
}
