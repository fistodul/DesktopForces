#include "Game/BattleRoyale/BattleRoyale.h"
#include "Game/BattleRoyale/BattleRoyaleActors.h"
#include "DesktopForces.h"
#include "Game/DFGameState.h"
#include "Game/DFPlayerState.h"
#include "Player/DFCharacter.h"
#include "Vehicles/DFVehicle.h"
#include "Weapons/DFWeapons.h"
#include "World/DFMapActors.h"
#include "Weapons/Deagle/Deagle.h"
#include "Weapons/GrenadeLauncher/GrenadeLauncher.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/PlayerStart.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

ABattleRoyale::ABattleRoyale()
{
	GameName = TEXT("Battle Royale");
	MFGameClass = TEXT("battleRoyale");
	NumTeams = 0;
	FragLimit = 12;   // the community servers' MobileForces.ini (battleRoyale: 30)
	TimeLimit = 12;
	// weapPackage / weapClass 0-9 and the MF-mods weapons this game has (deagle, grenlaunch)
	for (UClass* C : { APistol::StaticClass(), AMachineGun::StaticClass(), AShotgun::StaticClass(),
		ARifle::StaticClass(), AHeavyMachineGun::StaticClass(), ARocketLauncher::StaticClass(),
		AGrenades::StaticClass(), ATripBombs::StaticClass(), AAdrenalineShot::StaticClass(),
		AArmour::StaticClass(), ADeagle::StaticClass(), AGrenadeLauncher::StaticClass() })
	{
		Items.Add(TSoftClassPtr<ADFWeapon>(C));
	}
}

void ABattleRoyale::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	MaxDroppedWeapons = FMath::Max(MaxDroppedWeapons, 160);   // item boxes put out a lot
}

UClass* ABattleRoyale::PickItem() const
{
	return Items.Num() ? Items[FMath::RandRange(0, Items.Num() - 1)].LoadSynchronous() : nullptr;
}

bool ABattleRoyale::RandomSpot(FVector& Out) const
{
	// getRandLoc + the trace down: a random spot on the nav mesh inside the playable area (the
	// box around starts and gameplay actors: skips the sky box), never in water
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation L;
	if (!Nav || !Nav->GetRandomPoint(L) || !Playable.IsInsideXY(L.Location) || L.Location.Z < Playable.Min.Z || L.Location.Z > Playable.Max.Z)
	{
		return false;
	}
	for (TActorIterator<APhysicsVolume> It(GetWorld()); It; ++It)
	{
		if (It->bWaterVolume && It->EncompassesPoint(L.Location))
		{
			return false;
		}
	}
	Out = L.Location;
	return true;
}

void ABattleRoyale::HandleMatchHasStarted()
{
	// the playable area and the safe zone's centre (the domination timer, else the middle of the starts)
	Playable = FBox(ForceInit);
	Starts.Reset();
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		Playable += It->GetActorLocation();
		Starts.Add(It->GetActorLocation());
	}
	for (TActorIterator<ADFLegacyActor> It(GetWorld()); It; ++It)
	{
		Playable += It->GetActorLocation();
	}
	Playable = Playable.ExpandBy(FVector(2000.f, 2000.f, 1500.f));
	const TArray<ADFLegacyActor*> Timers = ADFLegacyActor::FindAll(GetWorld(), TEXT("RageDominationTimer"));
	FVector Centre = FVector::ZeroVector;
	for (const FVector& S : Starts)
	{
		Centre += S / float(FMath::Max(1, Starts.Num()));
	}
	if (Timers.Num())
	{
		Centre = Timers[0]->GetActorLocation();
	}
	SpawnItems();
	if (bEnableSafeZone)
	{
		SafeZone = GetWorld()->SpawnActor<ABRSafeZone>(ABRSafeZone::StaticClass(), FTransform(Centre));
		SafeZone->StartRadius = SafeZone->Radius = SafeZoneRadius;
		SafeZone->MinRadius = MinSafeZoneRadius;
		SafeZone->ShrinkRate = SafeZoneShrinkRate;
		SafeZone->OutsideDamage = SafeZoneOutsideDamage;
		SafeZone->bShrinking = true;   // StartMatch: shrinking = safeZoneIsShrinking
	}
	Super::HandleMatchHasStarted();
}

void ABattleRoyale::SpawnItems()
{
	// doSpawning: GameItemCount spots, a box after every ItemBoxEvery - 1 towers
	TArray<FVector> Used = Starts;
	int32 Towers = 0, Boxes = 0;
	for (int32 Try = 0; Towers + Boxes < GameItemCount && Try < GameItemCount * 8; ++Try)
	{
		FVector At;
		if (!RandomSpot(At) || Used.ContainsByPredicate([&](const FVector& U) { return FVector::DistSquared(U, At) < FMath::Square(ItemSpacing); }))
		{
			continue;
		}
		Used.Add(At);
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FRotator Yaw(0.f, FMath::FRandRange(0.f, 360.f), 0.f);
		if ((Towers + Boxes + 1) % FMath::Max(1, ItemBoxEvery) == 0)
		{
			GetWorld()->SpawnActor<ABRItemBox>(ABRItemBox::StaticClass(), At + FVector(0, 0, 60.f), Yaw, SP);
			++Boxes;
		}
		else
		{
			GetWorld()->SpawnActor<ABRTower>(ABRTower::StaticClass(), At, Yaw, SP);
			++Towers;
		}
	}
	UE_LOG(LogDF, Log, TEXT("Battle Royale: %d towers, %d item boxes"), Towers, Boxes);
}

void ABattleRoyale::ClearItems()
{
	// doMapCleanup: boxes, towers, platforms, builder ammo
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->IsA<ABRItemBox>() || It->IsA<ABRTower>() || It->IsA<ABRBuilderAmmo>())
		{
			It->Destroy();
		}
	}
}

void ABattleRoyale::RestartPlayer(AController* NewPlayer)
{
	if (bDisableRespawn && !bRestartingRound && DeadThisRound.Contains(NewPlayer))
	{
		return;   // playerWaiting until the round restarts
	}
	Super::RestartPlayer(NewPlayer);
	ADFCharacter* C = NewPlayer ? Cast<ADFCharacter>(NewPlayer->GetPawn()) : nullptr;
	FVector Ground;
	bool bFound = false;
	for (int32 Try = 0; C && !bFound && Try < 32; ++Try)
	{
		bFound = RandomSpot(Ground);
	}
	if (!bFound)
	{
		return;   // stays at its player start
	}
	// restartPlayer: a random spot, from the air (airSpawn) or on the ground; the drop does no harm
	FVector At = Ground + FVector(0, 0, C->GetSimpleCollisionHalfHeight() + 20.f);
	if (bAirSpawn)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DFBRDrop), false, C);
		const FVector Top = Ground + FVector(0, 0, 2500.f);
		const float Ceiling = GetWorld()->LineTraceSingleByChannel(Hit, At, Top, ECC_WorldStatic, Params) ? Hit.Location.Z - 150.f : Top.Z;
		At.Z = FMath::Max(At.Z, Ceiling);
		C->bNoFallDamageOnce = true;
	}
	C->TeleportTo(At, FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), false, true);
	if (AController* Ctrl = C->GetController())
	{
		Ctrl->SetControlRotation(C->GetActorRotation());
	}
}

void ABattleRoyale::SetPlayerDefaults(APawn* PlayerPawn)
{
	Super::SetPlayerDefaults(PlayerPawn);
	if (ADFCharacter* C = Cast<ADFCharacter>(PlayerPawn); C && StartHealth > 0)
	{
		C->MaxHealth = StartHealth;
		C->Heal(StartHealth);
	}
}

void ABattleRoyale::ScoreKill(ADFPlayerState* Killer, ADFPlayerState* Victim)
{
	if (!bScoreLastedRounds)
	{
		Super::ScoreKill(Killer, Victim);
	}
}

int32 ABattleRoyale::AliveCount(ADFCharacter** OutLast) const
{
	int32 N = 0;
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && It->GetController())
		{
			++N;
			if (OutLast)
			{
				*OutLast = *It;
			}
		}
	}
	return N;
}

void ABattleRoyale::Killed(AController* Killer, AController* Victim, ADFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType)
{
	if (Victim)
	{
		DeadThisRound.Add(Victim);
	}
	Super::Killed(Killer, Victim, VictimPawn, DamageType);
	ADFCharacter* Last = nullptr;
	if (bRoundOver || !IsMatchInProgress() || AliveCount(&Last) > 1)
	{
		return;
	}
	// the last one standing: round over (+1 with scoreLastedRounds), everyone back in 3 s
	bRoundOver = true;
	ADFPlayerState* PS = Last ? Last->GetPlayerState<ADFPlayerState>() : nullptr;
	if (PS)
	{
		Event(FString::Printf(TEXT("%s survived the round!"), *PS->GetPlayerName()), DF::NoTeam);
		if (bScoreLastedRounds)
		{
			PS->SetScore(PS->GetScore() + 1.f);
			if (FragLimit > 0 && PS->GetScore() >= FragLimit)
			{
				FinishMatch(PS->GetPlayerName());
				return;
			}
		}
	}
	GetWorldTimerManager().SetTimer(RoundTimer, this, &ABattleRoyale::NextRound, 3.f, false);
}

void ABattleRoyale::Logout(AController* Exiting)
{
	DeadThisRound.Remove(Exiting);
	Super::Logout(Exiting);
}

void ABattleRoyale::NextRound()
{
	if (!IsMatchInProgress())
	{
		return;
	}
	// restartRound: clean up, cars gone (their generators bring new ones), new loot, full zone
	bRoundOver = false;
	DeadThisRound.Reset();
	ClearItems();
	for (TActorIterator<ADFVehicle> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}
	SpawnItems();
	if (SafeZone)
	{
		SafeZone->Radius = SafeZone->StartRadius;
	}
	TGuardValue<bool> Restarting(bRestartingRound, true);
	RestartRound();
}

void ABattleRoyale::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const
{
	const ADFCharacter* C = Bot ? Cast<ADFCharacter>(Bot->GetPawn()) : nullptr;
	if (!C)
	{
		return;
	}
	// back into the zone first, then the nearest unopened box while poorly armed
	if (SafeZone && !SafeZone->IsInside(C->GetActorLocation()))
	{
		OutGoals.Add(SafeZone);
		return;
	}
	if (C->GetInventory().Num() > 3)
	{
		return;
	}
	AActor* Best = nullptr;
	float BestD = FMath::Square(6000.f);
	for (TActorIterator<ABRItemBox> It(GetWorld()); It; ++It)
	{
		const float D = FVector::DistSquared(It->GetActorLocation(), C->GetActorLocation());
		if (!It->IsOpened() && D < BestD && (!SafeZone || SafeZone->IsInside(It->GetActorLocation())))
		{
			Best = *It;
			BestD = D;
		}
	}
	if (Best)
	{
		OutGoals.Add(Best);
	}
}
