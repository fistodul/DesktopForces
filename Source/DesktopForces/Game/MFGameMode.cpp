#include "Game/MFGameMode.h"
#include "Vehicles/MFVehicle.h"
#include "Weapons/MFTripBomb.h"
#include "Weapons/MFProjectile.h"
#include "Weapons/MFWeapon.h"
#include "Player/MFCharacter.h"
#include "EngineUtils.h"
#include "DesktopForces.h"
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
	ArmedAPC = UGameplayStatics::GetIntOption(Options, TEXT("ArmedAPC"), ArmedAPC);
	bChangeDamage = UGameplayStatics::GetIntOption(Options, TEXT("ChangeDamage"), bChangeDamage ? 1 : 0) != 0;
	bJeeps = UGameplayStatics::GetIntOption(Options, TEXT("Jeeps"), bJeeps ? 1 : 0) != 0;
	bTurboCars = UGameplayStatics::GetIntOption(Options, TEXT("TurboCars"), bTurboCars ? 1 : 0) != 0;
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
		PS->SetTeam(IsTeamGame() ? ChooseTeam(NewPlayer, MF::NoTeam) : MF::NoTeam);
	}
	Super::PostLogin(NewPlayer);
	// RageDeathMatch.Timer: a second in, the order (PlayStartUpMessage); a moment later the
	// time left (InstructionSoundTime 1.5 + 0.75)
	if (AMFPlayerController* MPC = Cast<AMFPlayerController>(NewPlayer))
	{
		TWeakObjectPtr<AMFPlayerController> Weak(MPC);
		FTimerHandle Order, Time;
		GetWorldTimerManager().SetTimer(Order, FTimerDelegate::CreateWeakLambda(this, [this, Weak]()
		{
			if (Weak.IsValid())
			{
				Weak->ClientAnnouncer(GetInstructionSound(Weak.Get()));
			}
		}), 1.f, false);
		GetWorldTimerManager().SetTimer(Time, FTimerDelegate::CreateWeakLambda(this, [this, Weak]()
		{
			const AMFGameState* GS = GetGameState<AMFGameState>();
			if (Weak.IsValid() && GS && GS->GetRemainingTime() > 0)
			{
				Weak->ClientAnnouncer(AMFPlayerController::TimeRemainingLine(GS->GetRemainingTime()));
			}
		}), 2.5f, false);
	}
}

void AMFGameMode::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();
	AddBots(BotCount);
	if (bVehicles)
	{
		TArray<FVector> Exclude;
		GetVehicleExclusions(Exclude);
		AMFVehicleGenerator::SpawnAll(GetWorld(), Exclude);   // the map's Buggy/Humvee/Truck/ArmadilloGenerators
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
	// RageTeamGame.FindPlayerStart (bSpawnInTeamArea): the team's own starts; no team yet = team 0
	const AMFPlayerState* PS = Player ? Player->GetPlayerState<AMFPlayerState>() : nullptr;
	const uint8 Team = PS && PS->HasTeam() ? PS->GetTeam() : uint8(0);
	return Start->TeamNumber == Team;
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
		// e.g. team 3 on a map built for 2 teams: any start but the captains' (warehouse: the
		// captain's room has neutral starts too, for deathmatch only)
		for (AMFPlayerStart* S : All)
		{
			if (S->bEnabled && !S->bCaptainStart && (!IsTeamGame() || !S->IsNeutral()))
			{
				Candidates.Add(S);
			}
		}
	}
	if (Candidates.Num() == 0)
	{
		Candidates = All;
	}
	if (Candidates.Num() == 0)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}
	AActor* Best = nullptr;
	float BestScore = -FLT_MAX;
	const UClass* PawnClass = GetDefaultPawnClassForController(Player);
	const APawn* PawnCDO = PawnClass ? PawnClass->GetDefaultObject<APawn>() : nullptr;
	for (AMFPlayerStart* Start : Candidates)
	{
		float Score = Start == LastStartSpot.Get() ? -10000.f : 3000.f * FMath::FRand();
		if (PawnCDO && GetWorld()->EncroachingBlockingGeometry(PawnCDO, Start->GetActorLocation(), Start->GetActorRotation()))
		{
			Score -= 500000.f;              // in geometry: only when nothing else is left
		}
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

APawn* AMFGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	// UE1 Spawn ran FindSpot and some MF starts sit low over a crate or a step (mf-Tropics):
	// when UE's own push-out fails, try again a little higher instead of leaving the player
	// without a body
	FTransform T = SpawnTransform;
	for (int32 Try = 0; Try < 4; ++Try, T.AddToTranslation(FVector(0.f, 0.f, 40.f)))
	{
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.Instigator = GetInstigator();
		SpawnInfo.ObjectFlags |= RF_Transient;
		if (APawn* P = GetWorld()->SpawnActor<APawn>(GetDefaultPawnClassForController(NewPlayer), T, SpawnInfo))
		{
			return P;
		}
	}
	UE_LOG(LogMF, Warning, TEXT("No room for a pawn at %s"), *SpawnTransform.GetLocation().ToString());
	return nullptr;
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

UClass* AMFGameMode::GetVehicleClassFor(UClass* StockClass) const
{
	if (ArmedAPC > 0 && StockClass && StockClass->GetName() == TEXT("MFVehicle_Armadillo"))
	{
		const TCHAR* Path = ArmedAPC >= 2 ? TEXT("/Script/DesktopForces.MFVehicle_ArmedAPCRocket") : TEXT("/Script/DesktopForces.MFVehicle_ArmedAPC");
		if (UClass* Replacement = LoadClass<AActor>(nullptr, Path))
		{
			return Replacement;
		}
	}
	if (bJeeps && StockClass && StockClass->GetName() == TEXT("MFVehicle_Truck"))
	{
		if (UClass* Replacement = LoadClass<AActor>(nullptr, TEXT("/Script/DesktopForces.MFVehicle_Jeep")))
		{
			return Replacement;   // mutPack addVehicles / replaceVehicles style swap
		}
	}
	return StockClass;
}

void AMFGameMode::LoadoutDenied(APlayerController* PC) const
{
	if (PC)
	{
		PC->ClientMessage(NSLOCTEXT("MF", "NoLoadout", "No loadout in this game").ToString());
	}
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
			PS->SetTeam(IsTeamGame() ? ChooseTeam(Bot, Def.PreferredTeam) : MF::NoTeam);
		}
		++NumBots;
		RestartPlayer(Bot);
	}
}

// --- damage & kills ----------------------------------------------------------------

float AMFGameMode::ModifyDamage(float Damage, AMFCharacter* Victim, AController* Instigator, AActor* DamageCauser) const
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
	// changeDamage.mutatorTakeDamage: players' rockets x2; bots / 5 on people in vehicles
	if (bChangeDamage && Victim)
	{
		if (Instigator->IsPlayerController() && DamageCauser && DamageCauser->IsA<AMFProjectile_Rocket>())
		{
			Damage *= RocketDamageScaleOnFoot;
		}
		else if (!Instigator->IsPlayerController() && Victim->IsPlayerControlled() && Cast<AMFVehicle>(Victim->GetAttachParentActor()))
		{
			Damage *= BotOccupantDamageScale;
		}
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
	const bool bEndsSpree = VictimPS && VictimPS->Spree > 4;   // RageDeathMatch.ScoreKill: Other.Spree > 4 -> EndSpree
	ScoreKill(KillerPS, VictimPS);

	BroadcastLocalized(this, UMFDeathMessage::StaticClass(), 0, KillerPS, VictimPS, DamageType.Get());
	if (bEndsSpree)
	{
		BroadcastLocalized(this, UMFAnnouncement::StaticClass(), UMFAnnouncement::SpreeEnded, KillerPS != VictimPS ? KillerPS : nullptr, VictimPS);
	}
	if (KillerPS && KillerPS != VictimPS)
	{
		if (!bFirstBlood)
		{
			bFirstBlood = true;
			BroadcastLocalized(this, UMFAnnouncement::StaticClass(), UMFAnnouncement::FirstBlood, KillerPS);
		}
		// NotifySpree: at 5, 10, 15, 20 and 25 kills in a row
		if (KillerPS->Spree % 5 == 0 && KillerPS->Spree >= 5 && KillerPS->Spree <= 25)
		{
			BroadcastLocalized(this, UMFAnnouncement::StaticClass(), UMFAnnouncement::Spree + KillerPS->Spree / 5 - 1, KillerPS);
		}
		// RageDeathMatch.Killed: 'Decapitated' -> RageMessageDecapitation to the killer
		if (APlayerController* KPC = Cast<APlayerController>(Killer); KPC && DamageType && DamageType->IsChildOf(UMFDamage_Headshot::StaticClass()))
		{
			KPC->ClientReceiveLocalizedMessage(UMFAnnouncement::StaticClass(), UMFAnnouncement::Headshot, KillerPS);
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
					FinishMatch(FString::Printf(TEXT("%s Team Wins!"), *GetGameState<AMFGameState>()->GetTeamName(T).ToString()));
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
		FinishMatch(bTie ? TEXT("DRAW") : FString::Printf(TEXT("%s Team Wins!"), *GetGameState<AMFGameState>()->GetTeamName(Best).ToString()));
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
	return Order >= 0 && Order < 5 ? Lines[Order] : FString();
}

FName AMFGameMode::GetOrderSound(int32 Order) const
{
	static const TCHAR* Lines[] = { TEXT("Defend_the_command_post"), TEXT("Defend_this_area"), TEXT("Attack_the_enemy_base"),
		TEXT("Cover_me"), TEXT("You_re_on_your_own") };
	return Order >= 0 && Order < 5 ? FName(Lines[Order]) : NAME_None;
}

void AMFGameMode::GiveOrder(AController* Giver, int32 Order, APlayerState* Target)
{
	const AMFPlayerState* GiverPS = Giver ? Giver->GetPlayerState<AMFPlayerState>() : nullptr;
	if (!GiverPS)
	{
		return;
	}
	// MF chat line: "Sender: Heywood, Defend this area." (to the team), said aloud; "Improvise"
	// comes out as "Fire at will" 40% of the time (SetOrderMessage: OrderSound / OrderString 15)
	const bool bFireAtWill = Order == 4 && FMath::FRand() < 0.4f;
	const FString Line = bFireAtWill ? TEXT("Fire at will.") : GetOrderString(Order);
	BroadcastChat(Giver, Target ? FString::Printf(TEXT("%s, %s"), *Target->GetPlayerName(), *Line) : Line, true);
	AMFCharacter* Speaker = Cast<AMFCharacter>(Giver->GetPawn());
	if (const AMFVehicle* V = Cast<AMFVehicle>(Giver->GetPawn()))
	{
		Speaker = V->GetDriver();   // the soldier at the wheel
	}
	if (Speaker)
	{
		Speaker->MulticastVoice(bFireAtWill ? FName(TEXT("Fire_At_Will")) : GetOrderSound(Order));
	}
	// RagePlayerVoicePack AckSound / AckString
	static const TCHAR* Acks[] = { TEXT("Acknowledged"), TEXT("Roger that"), TEXT("Moving out"), TEXT("I'm on my way") };
	static const TCHAR* AckSounds[] = { TEXT("Acknowledged"), TEXT("Rodger_that"), TEXT("Moving_out"), TEXT("I_m_on_my_way") };
	for (TActorIterator<AMFBotController> It(GetWorld()); It; ++It)
	{
		const AMFPlayerState* PS = It->GetPlayerState<AMFPlayerState>();
		if (!PS || PS == GiverPS || (IsTeamGame() && PS->GetTeam() != GiverPS->GetTeam()) || (Target && PS != Target))
		{
			continue;
		}
		It->SetOrder(static_cast<EMFBotOrder>(Order), Giver->GetPawn());
		// the bot answers its team once the order has been spoken (SetAckMessage: 3 + FRand s)
		TWeakObjectPtr<AMFBotController> Bot = *It;
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, Bot]()
		{
			if (Bot.IsValid())
			{
				const int32 Ack = FMath::RandRange(0, 3);
				BroadcastChat(Bot.Get(), Acks[Ack], true);
				if (AMFCharacter* C = Cast<AMFCharacter>(Bot->GetPawn()))
				{
					C->MulticastVoice(AckSounds[Ack]);
				}
			}
		}), 3.f + FMath::FRand(), false);
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
	// RPlayer.PlayWinMessage: "Winner" for the best team / player, "You lose" for the rest
	const AMFGameState* GS = GetGameState<AMFGameState>();
	int32 BestTeam = INDEX_NONE;
	bool bTie = false;
	for (int32 T = 0; GS && T < GS->TeamScores.Num(); ++T)
	{
		if (BestTeam == INDEX_NONE || GS->TeamScores[T] > GS->TeamScores[BestTeam])
		{
			BestTeam = T;
			bTie = false;
		}
		else if (GS->TeamScores[T] == GS->TeamScores[BestTeam])
		{
			bTie = true;
		}
	}
	const APlayerState* Top = nullptr;
	for (const APlayerState* PS : GS ? GS->PlayerArray : TArray<TObjectPtr<APlayerState>>())
	{
		Top = !Top || PS->GetScore() > Top->GetScore() ? PS : Top;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AMFPlayerController* MPC = Cast<AMFPlayerController>(It->Get());
		const AMFPlayerState* PS = MPC ? MPC->GetPlayerState<AMFPlayerState>() : nullptr;
		if (PS)
		{
			const bool bWon = IsTeamGame() ? (!bTie && PS->GetTeam() == BestTeam) : PS == Top;
			MPC->ClientAnnouncer(IsTeamGame() && bTie ? TEXT("Draw") : (bWon ? TEXT("Winner") : TEXT("You_lose")));
		}
	}
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
		if (It->IsA<AMFTripBomb>() || It->IsA<AMFProjectile>() || (It->IsA<AMFWeapon>() && Cast<AMFWeapon>(*It)->IsDropped() && !Cast<AMFWeapon>(*It)->bMapPickup))
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

void AMFGameMode::Event(const FString& Text, uint8 ColourTeam, FName Voice)
{
	UE_LOG(LogMF, Display, TEXT("%s: %s"), *GameName, *Text);
	const FName Type(*FString::Printf(TEXT("Event%d"), ColourTeam));
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			PC->ClientTeamMessage(nullptr, Text, Type);
			if (AMFPlayerController* MPC = Cast<AMFPlayerController>(PC); MPC && !Voice.IsNone())
			{
				MPC->ClientAnnouncer(Voice);
			}
		}
	}
}
