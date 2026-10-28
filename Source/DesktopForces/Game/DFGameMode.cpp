#include "Game/DFGameMode.h"
#include "World/MineGenerator.h"
#include "AI/DFNavigationSystem.h"
#include "Vehicles/DFVehicle.h"
#include "Vehicles/Armadillo/Armadillo.h"
#include "Vehicles/Buggy/Buggy.h"
#include "Vehicles/Humvee/Humvee.h"
#include "Vehicles/Truck/Truck.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Weapons/TripBombs/TripBomb.h"
#include "Weapons/DFProjectile.h"
#include "Weapons/DFWeapon.h"
#include "Player/DFCharacter.h"
#include "EngineUtils.h"
#include "DesktopForces.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/StaticMeshActor.h"
#include "Game/DFGameState.h"
#include "Game/DFMessages.h"
#include "Game/DFPlayerState.h"
#include "Game/DFPlayerStart.h"
#include "AI/DFBotController.h"
#include "World/DFBotSpot.h"
#include "Core/DFGameSettings.h"
#include "Loadout/DFLoadout.h"
#include "Player/DFCharacter.h"
#include "Player/DFPlayerController.h"
#include "UI/DFHUD.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

ADFGameMode::ADFGameMode() {
	DefaultPawnClass = ADFCharacter::StaticClass();
	PlayerControllerClass = ADFPlayerController::StaticClass();
	PlayerStateClass = ADFPlayerState::StaticClass();
	GameStateClass = ADFGameState::StaticClass();
	HUDClass = ADFHUD::StaticClass();
	bDelayedStart = false;
	bUseSeamlessTravel = false;
}

void ADFGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) {
	Super::InitGame(MapName, Options, ErrorMessage);
	EnsureKillZ();
	NumTeams = FMath::Max(0, UGameplayStatics::GetIntOption(Options, TEXT("Teams"), NumTeams));
	ScoreLimit = FMath::Max(0, UGameplayStatics::GetIntOption(Options, TEXT("ScoreLimit"), ScoreLimit));
	TimeLimit = UGameplayStatics::GetIntOption(Options, TEXT("TimeLimit"), TimeLimit);
	TimeLimitSeconds = UGameplayStatics::GetIntOption(Options, TEXT("TimeLimitSecs"), TimeLimitSeconds);
	RespawnWait = UGameplayStatics::GetIntOption(Options, TEXT("RespawnWait"), RespawnWait);
	BotCount = UGameplayStatics::GetIntOption(Options, TEXT("Bots"), BotCount);
	if (const FString V = UGameplayStatics::ParseOption(Options, TEXT("Vehicles")); !V.IsEmpty()) {
		bVehicles = V != TEXT("0");
		AllVehicles = V.IsNumeric() ? FString() : V; // 1: the map's own
	}
	if (UGameplayStatics::HasOption(Options, TEXT("APCs")))
		APCs = UGameplayStatics::ParseOption(Options, TEXT("APCs"));
	bAllowCheats = UGameplayStatics::GetIntOption(Options, TEXT("Cheats"), bAllowCheats ? 1 : 0) != 0;
	bParachutes = UGameplayStatics::GetIntOption(Options, TEXT("Parachutes"), bParachutes ? 1 : 0) != 0;
	if (UGameplayStatics::HasOption(Options, TEXT("Trucks")))
		Trucks = UGameplayStatics::ParseOption(Options, TEXT("Trucks"));
	bAddVehicles = UGameplayStatics::GetIntOption(Options, TEXT("AddVehicles"), bAddVehicles ? 1 : 0) != 0;
	bTurboCars = UGameplayStatics::GetIntOption(Options, TEXT("TurboCars"), bTurboCars ? 1 : 0) != 0;
	bOPWeapons = UGameplayStatics::GetIntOption(Options, TEXT("OPWeapons"), bOPWeapons ? 1 : 0) != 0;
	bImprovedRL = UGameplayStatics::GetIntOption(Options, TEXT("ImprovedRL"), bImprovedRL ? 1 : 0) != 0;
	bBoomstick = UGameplayStatics::GetIntOption(Options, TEXT("Boomstick"), bBoomstick ? 1 : 0) != 0;
	if (const FString Banned = UGameplayStatics::ParseOption(Options, TEXT("NoWeapons")); !Banned.IsEmpty())
		Banned.ParseIntoArray(NoWeapons, TEXT(","));
	if (const FString G = UGameplayStatics::ParseOption(Options, TEXT("Gravity")); !G.IsEmpty())
		Gravity = FCString::Atof(*G);
	if (Gravity != 1.f) {
		// AWorldSettings::WorldGravityZ is replicated: clients fall the same
		AWorldSettings* WS = GetWorldSettings();
		WS->WorldGravityZ = UPhysicsSettings::Get()->DefaultGravityZ * FMath::Clamp(Gravity, 0.f, 10.f);
		WS->bWorldGravitySet = true;
	}
	const FString Skill = UGameplayStatics::ParseOption(Options, TEXT("BotSkill"));
	if (!Skill.IsEmpty())
		BotSkill = FMath::Clamp(FCString::Atof(*Skill), 0.f, 1.f);
}

void ADFGameMode::InitGameState() {
	Super::InitGameState();
	if (ADFGameState* GS = GetGameState<ADFGameState>()) {
		GS->NumTeams = NumTeams;
		GS->TeamScores.Init(0, NumTeams);
		GS->bCheatsAllowed = bAllowCheats;
		GS->bParachutes = bParachutes;
		GS->FireRate = bOPWeapons ? FMath::Clamp(OPFireRate, 0.1f, 20.f) : 1.f;
		GS->bInfiniteAmmo = bOPWeapons;
		GS->bImprovedRL = bImprovedRL;
		GS->bBoomstick = bBoomstick;
		for (const FString& Name : NoWeapons) {
			UClass* C = UClass::TryFindTypeSlow<UClass>(Name.TrimStartAndEnd());
			if (C && C->IsChildOf<ADFWeapon>())
				GS->BannedItems.Add(C);
		}
		GS->ScoreLimit = ScoreLimit;
		GS->TimeLimit = TimeLimit;
		GS->TimeLimitSeconds = TimeLimitSeconds;
		GS->RespawnWait = FMath::Max(1, RespawnWait);
		GS->GameName = GameName;
	}
}

FString ADFGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal) {
	const FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	if (Error.IsEmpty())
		if (ADFPlayerState* PS = NewPlayerController->GetPlayerState<ADFPlayerState>()) {
			PS->PreferredModel = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Model"), -1), -1, 1);
			const int32 Team = UGameplayStatics::GetIntOption(Options, TEXT("Team"), -1);
			PS->PreferredTeam = Team >= 0 && Team < NumTeams ? uint8(Team) : DF::NoTeam;
		}
	return Error;
}

void ADFGameMode::PostLogin(APlayerController* NewPlayer) {
	if (ADFPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<ADFPlayerState>() : nullptr) {
		PS->SetTeam(IsTeamGame() ? ChooseTeam(NewPlayer, PS->PreferredTeam) : DF::NoTeam);
		// the one playing on this machine runs the game: single player, a listen server's host
		PS->bAdmin = NewPlayer->IsLocalController();
	}
	Super::PostLogin(NewPlayer);
	// RageDeathMatch.Timer: a second in, the order (PlayStartUpMessage); a moment later the
	// time left (InstructionSoundTime 1.5 + 0.75)
	if (ADFPlayerController* MPC = Cast<ADFPlayerController>(NewPlayer)) {
		TWeakObjectPtr<ADFPlayerController> Weak(MPC);
		FTimerHandle Order, Time;
		GetWorldTimerManager().SetTimer(Order, FTimerDelegate::CreateWeakLambda(this, [this, Weak]() {
			if (Weak.IsValid())
				Weak->ClientAnnouncer(GetInstructionSound(Weak.Get()));
		}),
			1.f, false);
		GetWorldTimerManager().SetTimer(Time, FTimerDelegate::CreateWeakLambda(this, [this, Weak]() {
			const ADFGameState* GS = GetGameState<ADFGameState>();
			if (Weak.IsValid() && GS && GS->GetRemainingTime() > 0)
				Weak->ClientAnnouncer(ADFPlayerController::TimeRemainingLine(GS->GetRemainingTime()));
		}),
			2.5f, false);
	}
}

void ADFGameMode::HandleMatchHasStarted() {
	Super::HandleMatchHasStarted();
	if (ServerMessages.Num() && ServerMessageInterval > 0.f) {
		FTimerHandle T;
		GetWorldTimerManager().SetTimer(T, this, &ADFGameMode::SayServerMessages, ServerMessageInterval, true);
	}
	AddBots(BotCount);
	AMineGenerator::SpawnAll(GetWorld()); // mapped minefields / river splashes (mineGenerator.u)
	SpawnPlacedBots();
	if (bVehicles) {
		TArray<FVector> Exclude;
		GetVehicleExclusions(Exclude);
		AVehicleGenerator::SpawnAll(GetWorld(), Exclude); // the map's Buggy/Humvee/Truck/ArmadilloGenerators
	}
	GetWorldTimerManager().SetTimer(TimeLimitTimer, this, &ADFGameMode::CheckTimeLimit, 1.f, true);
	// the map is set up for this game type (actors gone by GameInfo.IsRelevant): the baked
	// navmesh stays as it is
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() {
		if (UDFNavigationSystem* Nav = FNavigationSystem::GetCurrent<UDFNavigationSystem>(GetWorld()))
			Nav->ForgetStartupChanges();
	}));
}

void ADFGameMode::CheckTimeLimit() {
	const ADFGameState* GS = GetGameState<ADFGameState>();
	if (GS && IsMatchInProgress() && GS->GetRemainingTime() == 0)
		OnTimeLimit();
}

uint8 ADFGameMode::PickTeam(uint8 Preferred) const {
	TArray<int32> Count;
	Count.Init(0, NumTeams);
	if (const ADFGameState* GS = GetGameState<ADFGameState>()) {
		for (APlayerState* PS : GS->PlayerArray)
			if (const ADFPlayerState* M = Cast<ADFPlayerState>(PS); M && Count.IsValidIndex(M->GetTeam()))
				Count[M->GetTeam()]++;
	}
	int32 Best = Count.IsValidIndex(Preferred) ? Preferred : 0;
	for (int32 T = 0; T < Count.Num(); ++T)
		if (Count[T] < Count[Best])
			Best = T;
	return uint8(Best);
}

// --- spawning ----------------------------------------------------------------------

bool ADFGameMode::IsStartAllowed(const ADFPlayerStart* Start, AController* Player) const {
	if (!Start->bEnabled || Start->bCaptainStart)
		return false; // captain starts: see ACaptains
	if (!IsTeamGame())
		return Start->IsNeutral();
	// RageTeamGame.FindPlayerStart (bSpawnInTeamArea): the team's own starts; no team yet = team 0
	const ADFPlayerState* PS = Player ? Player->GetPlayerState<ADFPlayerState>() : nullptr;
	const uint8 Team = PS && PS->HasTeam() ? PS->GetTeam() : uint8(0);
	return Start->TeamNumber == Team;
}

AActor* ADFGameMode::ChoosePlayerStart_Implementation(AController* Player) {
	// RageDeathMatch.FindPlayerStart: random score, avoid the last spot, avoid other players
	TArray<ADFPlayerStart*> Candidates, All;
	for (TActorIterator<ADFPlayerStart> It(GetWorld()); It; ++It) {
		All.Add(*It);
		if (IsStartAllowed(*It, Player))
			Candidates.Add(*It);
	}
	if (Candidates.Num() == 0) {
		// e.g. team 3 on a map built for 2 teams: any start but the captains' (warehouse: the
		// captain's room has neutral starts too, for deathmatch only)
		for (ADFPlayerStart* S : All)
			if (S->bEnabled && !S->bCaptainStart && (!IsTeamGame() || !S->IsNeutral()))
				Candidates.Add(S);
	}
	if (Candidates.Num() == 0)
		Candidates = All;
	if (Candidates.Num() == 0)
		return Super::ChoosePlayerStart_Implementation(Player);
	AActor* Best = nullptr;
	float BestScore = -FLT_MAX;
	const UClass* PawnClass = GetDefaultPawnClassForController(Player);
	const APawn* PawnCDO = PawnClass ? PawnClass->GetDefaultObject<APawn>() : nullptr;
	for (ADFPlayerStart* Start : Candidates) {
		float Score = Start == LastStartSpot.Get() ? -10000.f : 3000.f * FMath::FRand();
		if (PawnCDO && GetWorld()->EncroachingBlockingGeometry(PawnCDO, Start->GetActorLocation(), Start->GetActorRotation()))
			Score -= 500000.f; // in geometry: only when nothing else is left
		for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It) {
			if (!It->IsAlive() || It->GetController() == Player)
				continue;
			const float Dist = FVector::Dist(It->GetActorLocation(), Start->GetActorLocation());
			if (Dist < 126.f) // MF: CollisionRadius + CollisionHeight (56 uu)
				Score -= 1000000.f;
			else if (Dist < 4500.f) // MF: 2000 uu
				Score -= 22500.f - Dist;
		}
		if (Score > BestScore) {
			BestScore = Score;
			Best = Start;
		}
	}
	LastStartSpot = Best;
	return Best;
}

bool ADFGameMode::PlayerCanRestart_Implementation(APlayerController* Player) {
	return IsMatchInProgress() && Super::PlayerCanRestart_Implementation(Player);
}

void ADFGameMode::RestartPlayer(AController* NewPlayer) {
	RespawnTimers.Remove(NewPlayer);
	Super::RestartPlayer(NewPlayer);
	if (NewPlayer && !NewPlayer->GetPawn() && NewPlayer->PlayerState && !NewPlayer->PlayerState->IsSpectator()) {
		// no start had room (bots added in a crowd fill the spawn room): try again in a moment
		// rather than leaving them on the scoreboard without a body
		TWeakObjectPtr<AController> Weak(NewPlayer);
		FTimerHandle& H = RespawnTimers.FindOrAdd(NewPlayer);
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, Weak]() {
			if (AController* C = Weak.Get(); C && !C->GetPawn())
				RestartPlayer(C);
		}),
			1.f, false);
	}
}

APawn* ADFGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) {
	// UE1 Spawn ran FindSpot and some MF starts sit low over a crate or a step (mf-Tropics):
	// when UE's own push-out fails, try again a little higher instead of leaving the player
	// without a body
	FTransform T = SpawnTransform;
	for (int32 Try = 0; Try < 4; ++Try, T.AddToTranslation(FVector(0.f, 0.f, 40.f))) {
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.Instigator = GetInstigator();
		SpawnInfo.ObjectFlags |= RF_Transient;
		if (APawn* P = GetWorld()->SpawnActor<APawn>(GetDefaultPawnClassForController(NewPlayer), T, SpawnInfo))
			return P;
	}
	UE_LOG(LogDF, Warning, TEXT("No room for a pawn at %s"), *SpawnTransform.GetLocation().ToString());
	return nullptr;
}

void ADFGameMode::SetPlayerDefaults(APawn* PlayerPawn) {
	Super::SetPlayerDefaults(PlayerPawn);
	ADFCharacter* C = Cast<ADFCharacter>(PlayerPawn);
	if (!C)
		return;
	for (TSubclassOf<ADFWeapon> W : C->DefaultInventory)
		C->GiveWeapon(W, true);
	if (CanUseLoadout(C)) {
		if (ADFPlayerController* PC = Cast<ADFPlayerController>(C->GetController())) {
			C->BeginStartupLoadout();
			PC->EnterLoadout();
		}
	}
}

void ADFGameMode::EnsureKillZ() {
	// Maps built before KillZ was set (engine default: never) - 20 m under the lowest geometry
	AWorldSettings* WS = GetWorldSettings();
	if (!WS || WS->KillZ > -1.0e6f)
		return;
	float MinZ = TNumericLimits<float>::Max();
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It) {
		FVector Origin, Extent;
		It->GetActorBounds(false, Origin, Extent);
		MinZ = FMath::Min(MinZ, float(Origin.Z - Extent.Z));
	}
	if (MinZ < TNumericLimits<float>::Max()) {
		WS->bEnableWorldBoundsChecks = true;
		WS->KillZ = MinZ - 2000.f;
	}
}

bool ADFGameMode::AllowCheats(APlayerController* P) {
	return Super::AllowCheats(P) || bAllowCheats || IsAdmin(P);
}

bool ADFGameMode::IsAdmin(const AController* C) const {
	const ADFPlayerState* PS = C ? C->GetPlayerState<ADFPlayerState>() : nullptr;
	return PS && PS->bAdmin;
}

bool ADFGameMode::AdminLogin(APlayerController* PC, const FString& Password) {
	ADFPlayerState* PS = PC ? PC->GetPlayerState<ADFPlayerState>() : nullptr;
	if (!PS || AdminPassword.IsEmpty() || Password != AdminPassword) {
		UE_LOG(LogDF, Warning, TEXT("Admin login refused: %s"), PS ? *PS->GetPlayerName() : TEXT("?"));
		return false;
	}
	PS->bAdmin = true;
	PC->AddCheats(true);
	UE_LOG(LogDF, Log, TEXT("Admin login: %s"), *PS->GetPlayerName());
	Broadcast(this, FString::Printf(TEXT("%s logged in as admin"), *PS->GetPlayerName()), NAME_None);
	return true;
}

bool ADFGameMode::CanUseLoadout(ADFCharacter* Character) const {
	return UDFLoadoutLibrary::FindLoadoutSourceFor(Character) != nullptr;
}

void ADFGameMode::TravelAfterMatch() {
	if (!TravelToNextMap())
		RestartGame();
}

bool ADFGameMode::TravelToNextMap() {
	auto Split = [](const FString& URL) { TArray<FString> Parts; URL.ParseIntoArray(Parts, TEXT("?")); return Parts; };
	auto KeyOf = [](const FString& Option) { FString Key = Option; Option.Split(TEXT("="), &Key, nullptr); return Key; };
	// the position travels in the URL (?Rotation=i); a server started on any map begins at the top
	const int32 Here = UGameplayStatics::GetIntOption(OptionsString, TEXT("Rotation"), INDEX_NONE);
	// this server's own options (listen, Bots ...): what this map started with, minus what its
	// rotation entry set - each entry stands alone, as in MF's MapList - and the per-map ones
	TSet<FString> NotCarried = { TEXT("game"), TEXT("Rotation"), TEXT("Part"), TEXT("FirstCaptureTime") };
	if (MapRotation.IsValidIndex(Here)) {
		const TArray<FString> Current = Split(MapRotation[Here]);
		for (int32 I = 1; I < Current.Num(); ++I)
			NotCarried.Add(KeyOf(Current[I]));
	}
	const TArray<FString> ServerOptions = Split(OptionsString).FilterByPredicate([&](const FString& O) { return !NotCarried.Contains(KeyOf(O)); });
	for (int32 Step = 1; Step <= MapRotation.Num(); ++Step) {
		const int32 Index = (Here + Step) % MapRotation.Num();
		TArray<FString> Options = Split(MapRotation[Index]);
		if (Options.IsEmpty())
			continue;
		const FString Map = Options[0];
		Options.RemoveAt(0);
		FString URL = VoteMapURL(Map, UGameplayStatics::ParseOption(TEXT("?") + FString::Join(Options, TEXT("?")), TEXT("game")));
		if (URL.IsEmpty()) {
			UE_LOG(LogDF, Warning, TEXT("MapRotation: %s isn't here, skipped"), *MapRotation[Index]);
			continue;
		}
		// the entry's own options first, then the server's
		Options.Append(ServerOptions);
		TSet<FString> Keys = { TEXT("game") };
		for (const FString& Option : Options) {
			if (!Keys.Contains(KeyOf(Option))) {
				Keys.Add(KeyOf(Option));
				URL += TEXT("?") + Option;
			}
		}
		GetWorld()->ServerTravel(FString::Printf(TEXT("%s?Rotation=%d"), *URL, Index), false);
		return true;
	}
	return false;
}

void ADFGameMode::SayServerMessages() {
	for (int32 I = 0; I < FMath::Max(1, ServerMessageGroup); ++I) {
		Broadcast(this, ServerMessages[NextServerMessage % ServerMessages.Num()]);
		NextServerMessage = (NextServerMessage + 1) % ServerMessages.Num();
	}
}

UClass* ADFGameMode::GetVehicleClassFor(UClass* StockClass) const {
	auto VehicleNamed = [](const FString& Name) -> UClass* {
		UClass* C = Name.IsEmpty() ? nullptr : UClass::TryFindTypeSlow<UClass>(Name);
		return C && C->IsChildOf<ADFVehicle>() ? C : nullptr;
	};
	// ?Vehicles=Buggy / Random: every stock road car (exact classes: the trailer cab is a humvee)
	UClass* const Road[] = { ABuggy::StaticClass(), AHumvee::StaticClass(), ATruck::StaticClass(), AArmadillo::StaticClass() };
	if (!AllVehicles.IsEmpty() && TArrayView<UClass* const>(Road).Contains(StockClass)) {
		UClass* To = AllVehicles.Equals(TEXT("Random"), ESearchCase::IgnoreCase) ? Road[FMath::RandRange(0, UE_ARRAY_COUNT(Road) - 1)] : VehicleNamed(AllVehicles);
		StockClass = To ? To : StockClass;
	}
	// this map's swaps (UDFGameSettings::VehicleSwaps)
	const FString MapName = UWorld::RemovePIEPrefix(GetWorld()->GetMapName());
	for (const FDFVehicleSwap& Swap : UDFGameSettings::Get().VehicleSwaps) {
		if (StockClass && MapName.Equals(Swap.Map, ESearchCase::IgnoreCase) && AVehicleGenerator::ClassForMF(Swap.From) == StockClass) {
			if (UClass* To = AVehicleGenerator::ClassForMF(Swap.To))
				return To;
		}
	}
	// ?Trucks=Heli, ?APCs=ArmedAPC: what those generators make (mutPack replaceVehicles)
	UClass* To = StockClass == ATruck::StaticClass() ? VehicleNamed(Trucks) : StockClass == AArmadillo::StaticClass() ? VehicleNamed(APCs)
																													  : nullptr;
	return To ? To : StockClass;
}

void ADFGameMode::LoadoutDenied(APlayerController* PC) const {
	if (PC)
		PC->ClientMessage(NSLOCTEXT("MF", "NoLoadout", "No loadout in this game").ToString());
}

void ADFGameMode::AddBots(int32 Count, float Skill) {
	const TArray<FDFBotDef>& Roster = UDFGameSettings::Get().Bots;
	for (int32 i = 0; i < Count; ++i) {
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ADFBotController* Bot = GetWorld()->SpawnActor<ADFBotController>(SP);
		if (!Bot)
			continue;
		// Roster in order; past its end, recycle profiles with numbered names (no cap)
		FDFBotDef Def = Roster.Num() > 0 ? Roster[BotsAdded % Roster.Num()] : FDFBotDef();
		if (Roster.Num() == 0 || BotsAdded >= Roster.Num())
			Def.Name = FString::Printf(TEXT("%s%d"), Def.Name.IsEmpty() ? TEXT("Bot") : *Def.Name, BotsAdded / FMath::Max(1, Roster.Num()) + 1);
		++BotsAdded;
		Bot->Profile = Def;
		Bot->Skill = Skill >= 0.f ? Skill : BotSkill;
		if (ADFPlayerState* PS = Bot->GetPlayerState<ADFPlayerState>()) {
			PS->SetPlayerName(Def.Name);
			PS->SetIsABot(true);
			PS->SetTeam(IsTeamGame() ? ChooseTeam(Bot, Def.PreferredTeam) : DF::NoTeam);
		}
		++NumBots;
		RestartPlayer(Bot);
	}
}

void ADFGameMode::SpawnPlacedBots() {
	int32 N = 0;
	for (TActorIterator<ADFBotSpot> It(GetWorld()); It; ++It) {
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ADFBotController* Bot = GetWorld()->SpawnActor<ADFBotController>(SP);
		if (!Bot)
			continue;
		Bot->Profile.Name = FString::Printf(TEXT("Defender%d"), ++N);
		Bot->Profile.Loadout.Reset();
		for (const TSubclassOf<ADFWeapon>& W : It->Loadout)
			Bot->Profile.Loadout.Add(W.Get());
		Bot->Skill = BotSkill;
		Bot->bPlaced = true;
		Bot->bStationary = It->bStationary;
		if (ADFPlayerState* PS = Bot->GetPlayerState<ADFPlayerState>()) {
			PS->SetPlayerName(Bot->Profile.Name);
			PS->SetIsABot(true);
			PS->SetTeam(IsTeamGame() ? It->Team : DF::NoTeam);
		}
		RestartPlayerAtTransform(Bot, It->GetActorTransform());
		if (ADFCharacter* C = Cast<ADFCharacter>(Bot->GetPawn()))
			C->SetHealthAndMax(It->Health);
	}
	UE_CLOG(N > 0, LogDF, Log, TEXT("%d placed bots"), N);
}

// --- damage & kills ----------------------------------------------------------------

float ADFGameMode::ModifyDamage(float Damage, ADFCharacter* Victim, AController* InstigatedBy, AActor* DamageCauser) const {
	if (!InstigatedBy)
		return Damage;
	if (bHardCoreMode)
		Damage *= 1.5f;
	const ADFPlayerState* InstPS = InstigatedBy->GetPlayerState<ADFPlayerState>();
	if (IsTeamGame() && InstPS && Victim && Victim->GetController() != InstigatedBy && InstPS->GetTeam() == Victim->GetTeam())
		Damage *= FriendlyFireScale;
	// changeDamage.mutatorTakeDamage: players' rockets x2; bots / 5 on people in vehicles
	if (Victim) {
		if (InstigatedBy->IsPlayerController() && DamageCauser && DamageCauser->IsA<ARocket>())
			Damage *= RocketDamageScaleOnFoot;
		else if (!InstigatedBy->IsPlayerController() && Victim->IsPlayerControlled() && Cast<ADFVehicle>(Victim->GetAttachParentActor()))
			Damage *= BotOccupantDamageScale;
	}
	return Damage;
}

void ADFGameMode::Killed(AController* Killer, AController* Victim, ADFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType) {
	ADFPlayerState* KillerPS = Killer ? Killer->GetPlayerState<ADFPlayerState>() : nullptr;
	ADFPlayerState* VictimPS = Victim ? Victim->GetPlayerState<ADFPlayerState>() : (VictimPawn ? VictimPawn->GetPlayerState<ADFPlayerState>() : nullptr);
	if (VictimPS)
		VictimPS->DeathTime = GetGameState<ADFGameState>()->GetServerWorldTimeSeconds();
	const bool bEndsSpree = VictimPS && VictimPS->Spree > 4; // RageDeathMatch.ScoreKill: Other.Spree > 4 -> EndSpree
	ScoreKill(KillerPS, VictimPS);

	BroadcastLocalized(this, UDFDeathMessage::StaticClass(), 0, KillerPS, VictimPS, DamageType.Get());
	if (bEndsSpree)
		BroadcastLocalized(this, UDFAnnouncement::StaticClass(), UDFAnnouncement::SpreeEnded, KillerPS != VictimPS ? KillerPS : nullptr, VictimPS);
	if (KillerPS && KillerPS != VictimPS) {
		if (!bFirstBlood) {
			bFirstBlood = true;
			BroadcastLocalized(this, UDFAnnouncement::StaticClass(), UDFAnnouncement::FirstBlood, KillerPS);
		}
		// NotifySpree: at 5, 10, 15, 20 and 25 kills in a row
		if (KillerPS->Spree % 5 == 0 && KillerPS->Spree >= 5 && KillerPS->Spree <= 25)
			BroadcastLocalized(this, UDFAnnouncement::StaticClass(), UDFAnnouncement::Spree + KillerPS->Spree / 5 - 1, KillerPS);
		// RageDeathMatch.Killed: 'Decapitated' -> RageMessageDecapitation to the killer
		if (APlayerController* KPC = Cast<APlayerController>(Killer); KPC && DamageType && DamageType->IsChildOf(UDFDamage_Headshot::StaticClass()))
			KPC->ClientReceiveLocalizedMessage(UDFAnnouncement::StaticClass(), UDFAnnouncement::Headshot, KillerPS);
	}

	CheckEndConditions(KillerPS);
	const ADFBotController* Placed = Cast<ADFBotController>(Victim);
	if (Victim && !(Placed && Placed->bPlaced)) // placed pawns stay dead
		ScheduleRespawn(Victim);
}

void ADFGameMode::ScoreKill(ADFPlayerState* Killer, ADFPlayerState* Victim) {
	ADFGameState* GS = GetGameState<ADFGameState>();
	const bool bSuicide = !Killer || Killer == Victim;
	const bool bTeamKill = !bSuicide && IsTeamGame() && Victim && Killer->GetTeam() == Victim->GetTeam();
	if (Victim) {
		Victim->Deaths++;
		Victim->Spree = 0;
	}
	if (bSuicide) {
		if (Victim)
			Victim->SetScore(Victim->GetScore() - 1.f); // GameInfo.ScoreKill
	} else if (!bTeamKill) {
		Killer->Kills++;
		Killer->Spree++;
		Killer->SetScore(Killer->GetScore() + 1.f);
	}
	// Team scores (RageTeamGame.ScoreKill, bScoreTeamKills)
	if (!bScoreTeamKills || !GS || !Victim || !GS->TeamScores.IsValidIndex(Victim->GetTeam()))
		return;
	if (bSuicide) {
		GS->TeamScores[Victim->GetTeam()] -= 1;
	} else if (!bTeamKill) {
		if (GS->TeamScores.IsValidIndex(Killer->GetTeam()))
			GS->TeamScores[Killer->GetTeam()] += 1;
	} else if (FriendlyFireScale > 0.f) {
		GS->TeamScores[Victim->GetTeam()] -= 1;
		Killer->SetScore(Killer->GetScore() - 1.f);
	}
}

void ADFGameMode::CheckEndConditions(ADFPlayerState* LastKiller) {
	const ADFGameState* GS = GetGameState<ADFGameState>();
	if (IsTeamGame()) {
		if (GS && ScoreLimit > 0) {
			for (int32 T = 0; T < GS->TeamScores.Num(); ++T) {
				if (GS->TeamScores[T] >= ScoreLimit) {
					FinishMatch(FString::Printf(TEXT("%s Team Wins!"), *GetGameState<ADFGameState>()->GetTeamName(T).ToString()));
					return;
				}
			}
		}
	} else if (ScoreLimit > 0 && LastKiller && LastKiller->GetScore() >= ScoreLimit) {
		FinishMatch(FString::Printf(TEXT("%s Wins!"), *LastKiller->GetPlayerName()));
	}
}

void ADFGameMode::OnTimeLimit() {
	const ADFGameState* GS = GetGameState<ADFGameState>();
	if (IsTeamGame()) {
		int32 Best = 0;
		bool bTie = false;
		for (int32 T = 1; T < GS->TeamScores.Num(); ++T) {
			if (GS->TeamScores[T] > GS->TeamScores[Best]) {
				Best = T;
				bTie = false;
			} else if (GS->TeamScores[T] == GS->TeamScores[Best]) {
				bTie = true;
			}
		}
		FinishMatch(bTie ? TEXT("DRAW") : FString::Printf(TEXT("%s Team Wins!"), *GetGameState<ADFGameState>()->GetTeamName(Best).ToString()));
		return;
	}
	const TArray<ADFPlayerState*> Sorted = GS->GetSortedPlayers();
	FinishMatch(Sorted.Num() > 0 ? FString::Printf(TEXT("%s Wins!"), *Sorted[0]->GetPlayerName()) : TEXT("Time up"));
}

void ADFGameMode::ScheduleRespawn(AController* Victim) {
	if (!IsMatchInProgress())
		return;
	FTimerHandle& H = RespawnTimers.FindOrAdd(Victim);
	TWeakObjectPtr<AController> Weak(Victim);
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, Weak]() {
		if (AController* C = Weak.Get(); C && !C->GetPawn() && IsMatchInProgress())
			RestartPlayer(C);
	}),
		float(FMath::Max(1, RespawnWait)), false);
}

void ADFGameMode::BroadcastChat(AController* Sender, const FString& Msg, bool bTeam) {
	APlayerState* SenderPS = Sender ? Sender->PlayerState : nullptr;
	const ADFPlayerState* MFS = Cast<ADFPlayerState>(SenderPS);
	UE_LOG(LogDF, Display, TEXT("%s%s%s"), bTeam ? TEXT("[team] ") : TEXT(""), SenderPS ? *(SenderPS->GetPlayerName() + TEXT(": ")) : TEXT(""), *Msg);
	const bool bTeamOnly = bTeam && IsTeamGame() && MFS && MFS->HasTeam();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It) {
		APlayerController* PC = It->Get();
		const ADFPlayerState* PS = PC ? PC->GetPlayerState<ADFPlayerState>() : nullptr;
		if (PC && (!bTeamOnly || (PS && PS->GetTeam() == MFS->GetTeam())))
			PC->ClientTeamMessage(SenderPS, Msg, bTeamOnly ? FName(TEXT("TeamSay")) : FName(TEXT("Say")));
	}
}

void ADFGameMode::Say(const FString& Msg) {
	BroadcastChat(GetWorld()->GetFirstPlayerController(), Msg.Left(128), false);
}

FString ADFGameMode::GetOrderAbbrev(int32 Order) {
	// Rage.int OrderAbbrev / OrderString 0-4 (the menu)
	static const TCHAR* Abbrev[] = { TEXT("Defend!"), TEXT("Defend this area."), TEXT("Attack!"), TEXT("Cover me."), TEXT("Improvise!") };
	return Order >= 0 && Order < 5 ? Abbrev[Order] : FString();
}

FString ADFGameMode::GetOrderString(int32 Order) const {
	static const TCHAR* Lines[] = { TEXT("Defend the command post."), TEXT("Defend this area."),
		TEXT("Attack the enemy base."), TEXT("Cover me."), TEXT("Improvise, you're on your own.") };
	return Order >= 0 && Order < 5 ? Lines[Order] : FString();
}

FName ADFGameMode::GetOrderSound(int32 Order) const {
	static const TCHAR* Lines[] = { TEXT("Defend_the_command_post"), TEXT("Defend_this_area"), TEXT("Attack_the_enemy_base"),
		TEXT("Cover_me"), TEXT("You_re_on_your_own") };
	return Order >= 0 && Order < 5 ? FName(Lines[Order]) : NAME_None;
}

void ADFGameMode::GiveOrder(AController* Giver, int32 Order, APlayerState* Target) {
	const ADFPlayerState* GiverPS = Giver ? Giver->GetPlayerState<ADFPlayerState>() : nullptr;
	if (!GiverPS || !IsTeamGame())
		return; // orders go to team-mates; in deathmatch everyone is an enemy
	// MF chat line: "Sender: Heywood, Defend this area." (to the team), said aloud; "Improvise"
	// comes out as "Fire at will" 40% of the time (SetOrderMessage: OrderSound / OrderString 15)
	const bool bFireAtWill = Order == 4 && FMath::FRand() < 0.4f;
	const FString Line = bFireAtWill ? TEXT("Fire at will.") : GetOrderString(Order);
	BroadcastChat(Giver, Target ? FString::Printf(TEXT("%s, %s"), *Target->GetPlayerName(), *Line) : Line, true);
	ADFCharacter* Speaker = Cast<ADFCharacter>(Giver->GetPawn());
	if (const ADFVehicle* V = Cast<ADFVehicle>(Giver->GetPawn()))
		Speaker = V->GetDriver(); // the soldier at the wheel
	if (Speaker)
		Speaker->MulticastVoice(bFireAtWill ? FName(TEXT("Fire_At_Will")) : GetOrderSound(Order));
	// RagePlayerVoicePack AckSound / AckString
	static const TCHAR* Acks[] = { TEXT("Acknowledged"), TEXT("Roger that"), TEXT("Moving out"), TEXT("I'm on my way") };
	static const TCHAR* AckSounds[] = { TEXT("Acknowledged"), TEXT("Rodger_that"), TEXT("Moving_out"), TEXT("I_m_on_my_way") };
	for (TActorIterator<ADFBotController> It(GetWorld()); It; ++It) {
		const ADFPlayerState* PS = It->GetPlayerState<ADFPlayerState>();
		if (!PS || PS == GiverPS || (IsTeamGame() && PS->GetTeam() != GiverPS->GetTeam()) || (Target && PS != Target))
			continue;
		It->SetOrder(static_cast<EDFBotOrder>(Order), Giver->GetPawn());
		// the bot answers its team once the order has been spoken (SetAckMessage: 3 + FRand s)
		TWeakObjectPtr<ADFBotController> Bot = *It;
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, Bot]() {
			if (Bot.IsValid()) {
				const int32 Ack = FMath::RandRange(0, 3);
				BroadcastChat(Bot.Get(), Acks[Ack], true);
				if (ADFCharacter* C = Cast<ADFCharacter>(Bot->GetPawn()))
					C->MulticastVoice(AckSounds[Ack]);
			}
		}),
			3.f + FMath::FRand(), false);
	}
}

void ADFGameMode::FinishMatch(const FString& Winner) {
	if (!IsMatchInProgress())
		return;
	if (ADFGameState* GS = GetGameState<ADFGameState>())
		GS->WinnerText = Winner;
	EndMatch();
}

void ADFGameMode::HandleMatchHasEnded() {
	Super::HandleMatchHasEnded();
	GetWorldTimerManager().ClearTimer(TimeLimitTimer);
	for (auto& Pair : RespawnTimers)
		GetWorldTimerManager().ClearTimer(Pair.Value);
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It) {
		It->StopFire(false);
		It->StopFire(true);
		It->GetCharacterMovement()->DisableMovement();
	}
	BroadcastLocalized(this, UDFAnnouncement::StaticClass(), UDFAnnouncement::MatchEnd);
	// RPlayer.PlayWinMessage: "Winner" for the best team / player, "You lose" for the rest
	const ADFGameState* GS = GetGameState<ADFGameState>();
	int32 BestTeam = INDEX_NONE;
	bool bTie = false;
	for (int32 T = 0; GS && T < GS->TeamScores.Num(); ++T) {
		if (BestTeam == INDEX_NONE || GS->TeamScores[T] > GS->TeamScores[BestTeam]) {
			BestTeam = T;
			bTie = false;
		} else if (GS->TeamScores[T] == GS->TeamScores[BestTeam]) {
			bTie = true;
		}
	}
	const APlayerState* Top = nullptr;
	for (const APlayerState* PS : GS ? GS->PlayerArray : TArray<TObjectPtr<APlayerState>>())
		Top = !Top || PS->GetScore() > Top->GetScore() ? PS : Top;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It) {
		ADFPlayerController* MPC = Cast<ADFPlayerController>(It->Get());
		const ADFPlayerState* PS = MPC ? MPC->GetPlayerState<ADFPlayerState>() : nullptr;
		if (PS) {
			const bool bWon = IsTeamGame() ? (!bTie && PS->GetTeam() == BestTeam) : PS == Top;
			MPC->ClientAnnouncer(IsTeamGame() && bTie ? TEXT("Draw") : (bWon ? TEXT("Winner") : TEXT("You_lose")));
		}
	}
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, this, &ADFGameMode::TravelAfterMatch, float(RestartWait), false);
}

ATeamDeathMatch::ATeamDeathMatch() {
	ScoreLimit = 0;
	GameName = TEXT("Team DM");
	MFGameClass = TEXT("RageTeamGame");
	NumTeams = 2;
	TimeLimit = 12;
}

void ADFGameMode::RestartRound() {
	if (ADFGameState* GS = GetGameState<ADFGameState>())
		GS->RoundStartTime = GS->ElapsedTime;
	// what the last round left lying around: trip bombs, mines, thrown items, dropped weapons
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		if (It->IsA<ATripBomb>() || It->IsA<ADFProjectile>() || (It->IsA<ADFWeapon>() && Cast<ADFWeapon>(*It)->IsDropped() && !Cast<ADFWeapon>(*It)->bMapPickup))
			It->Destroy();
	// everyone starts fresh (MF RestartRound: discard inventory, StartupInLoadout)
	for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It) {
		AController* C = It->Get();
		if (!C || !C->PlayerState || C->PlayerState->IsSpectator())
			continue;
		if (APawn* P = C->GetPawn()) {
			if (ADFCharacter* MC = Cast<ADFCharacter>(P))
				MC->ClearInventory(false);
			P->Destroy();
		}
		RestartPlayer(C);
	}
}

void ADFGameMode::Event(const FString& Text, uint8 ColourTeam, FName Voice) {
	UE_LOG(LogDF, Display, TEXT("%s: %s"), *GameName, *Text);
	const FName Type(*FString::Printf(TEXT("Event%d"), ColourTeam));
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It) {
		if (APlayerController* PC = It->Get()) {
			PC->ClientTeamMessage(nullptr, Text, Type);
			if (ADFPlayerController* MPC = Cast<ADFPlayerController>(PC); MPC && !Voice.IsNone())
				MPC->ClientAnnouncer(Voice);
		}
	}
}
