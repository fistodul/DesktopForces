#include "Game/BattleRoyale/MFBattleRoyale.h"
#include "Game/BattleRoyale/MFBattleRoyaleActors.h"
#include "DesktopForces.h"
#include "Game/MFGameState.h"
#include "Game/MFPlayerState.h"
#include "Player/MFCharacter.h"
#include "Vehicles/MFVehicle.h"
#include "Weapons/MFWeapons.h"
#include "World/MFMapActors.h"
#include "Weapons/Deagle/MFWeapon_Deagle.h"
#include "Weapons/GrenadeLauncher/MFGrenadeLauncher.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/PlayerStart.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

AMFGameMode_BattleRoyale::AMFGameMode_BattleRoyale()
{
	GameName = TEXT("Battle Royale");
	MFGameClass = TEXT("battleRoyale");
	NumTeams = 0;
	FragLimit = 12;   // the community servers' MobileForces.ini (battleRoyale: 30)
	TimeLimit = 12;
	// weapPackage / weapClass 0-9 and the MF-mods weapons this game has (deagle, grenlaunch)
	for (UClass* C : { AMFWeapon_Pistol::StaticClass(), AMFWeapon_MachineGun::StaticClass(), AMFWeapon_Shotgun::StaticClass(),
		AMFWeapon_Rifle::StaticClass(), AMFWeapon_HeavyMachineGun::StaticClass(), AMFWeapon_RocketLauncher::StaticClass(),
		AMFWeapon_Grenades::StaticClass(), AMFWeapon_TripBombs::StaticClass(), AMFWeapon_AdrenalineShot::StaticClass(),
		AMFWeapon_Armour::StaticClass(), AMFWeapon_Deagle::StaticClass(), AMFWeapon_GrenadeLauncher::StaticClass() })
	{
		Items.Add(TSoftClassPtr<AMFWeapon>(C));
	}
}

void AMFGameMode_BattleRoyale::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	MaxDroppedWeapons = FMath::Max(MaxDroppedWeapons, 160);   // item boxes put out a lot
}

UClass* AMFGameMode_BattleRoyale::PickItem() const
{
	return Items.Num() ? Items[FMath::RandRange(0, Items.Num() - 1)].LoadSynchronous() : nullptr;
}

bool AMFGameMode_BattleRoyale::RandomSpot(FVector& Out) const
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

void AMFGameMode_BattleRoyale::HandleMatchHasStarted()
{
	// the playable area and the safe zone's centre (the domination timer, else the middle of the starts)
	Playable = FBox(ForceInit);
	Starts.Reset();
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		Playable += It->GetActorLocation();
		Starts.Add(It->GetActorLocation());
	}
	for (TActorIterator<AMFLegacyActor> It(GetWorld()); It; ++It)
	{
		Playable += It->GetActorLocation();
	}
	Playable = Playable.ExpandBy(FVector(2000.f, 2000.f, 1500.f));
	const TArray<AMFLegacyActor*> Timers = AMFLegacyActor::FindAll(GetWorld(), TEXT("RageDominationTimer"));
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
		SafeZone = GetWorld()->SpawnActor<AMFBRSafeZone>(AMFBRSafeZone::StaticClass(), FTransform(Centre));
		SafeZone->StartRadius = SafeZone->Radius = SafeZoneRadius;
		SafeZone->MinRadius = MinSafeZoneRadius;
		SafeZone->ShrinkRate = SafeZoneShrinkRate;
		SafeZone->OutsideDamage = SafeZoneOutsideDamage;
		SafeZone->bShrinking = true;   // StartMatch: shrinking = safeZoneIsShrinking
	}
	Super::HandleMatchHasStarted();
}

void AMFGameMode_BattleRoyale::SpawnItems()
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
			GetWorld()->SpawnActor<AMFBRItemBox>(AMFBRItemBox::StaticClass(), At + FVector(0, 0, 60.f), Yaw, SP);
			++Boxes;
		}
		else
		{
			GetWorld()->SpawnActor<AMFBRTower>(AMFBRTower::StaticClass(), At, Yaw, SP);
			++Towers;
		}
	}
	UE_LOG(LogMF, Log, TEXT("Battle Royale: %d towers, %d item boxes"), Towers, Boxes);
}

void AMFGameMode_BattleRoyale::ClearItems()
{
	// doMapCleanup: boxes, towers, platforms, builder ammo
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->IsA<AMFBRItemBox>() || It->IsA<AMFBRTower>() || It->IsA<AMFBRBuilderAmmo>())
		{
			It->Destroy();
		}
	}
}

void AMFGameMode_BattleRoyale::RestartPlayer(AController* NewPlayer)
{
	if (bDisableRespawn && !bRestartingRound && DeadThisRound.Contains(NewPlayer))
	{
		return;   // playerWaiting until the round restarts
	}
	Super::RestartPlayer(NewPlayer);
	AMFCharacter* C = NewPlayer ? Cast<AMFCharacter>(NewPlayer->GetPawn()) : nullptr;
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
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MFBRDrop), false, C);
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

void AMFGameMode_BattleRoyale::SetPlayerDefaults(APawn* PlayerPawn)
{
	Super::SetPlayerDefaults(PlayerPawn);
	if (AMFCharacter* C = Cast<AMFCharacter>(PlayerPawn); C && StartHealth > 0)
	{
		C->MaxHealth = StartHealth;
		C->Heal(StartHealth);
	}
}

void AMFGameMode_BattleRoyale::ScoreKill(AMFPlayerState* Killer, AMFPlayerState* Victim)
{
	if (!bScoreLastedRounds)
	{
		Super::ScoreKill(Killer, Victim);
	}
}

int32 AMFGameMode_BattleRoyale::AliveCount(AMFCharacter** OutLast) const
{
	int32 N = 0;
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
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

void AMFGameMode_BattleRoyale::Killed(AController* Killer, AController* Victim, AMFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType)
{
	if (Victim)
	{
		DeadThisRound.Add(Victim);
	}
	Super::Killed(Killer, Victim, VictimPawn, DamageType);
	AMFCharacter* Last = nullptr;
	if (bRoundOver || !IsMatchInProgress() || AliveCount(&Last) > 1)
	{
		return;
	}
	// the last one standing: round over (+1 with scoreLastedRounds), everyone back in 3 s
	bRoundOver = true;
	AMFPlayerState* PS = Last ? Last->GetPlayerState<AMFPlayerState>() : nullptr;
	if (PS)
	{
		Event(FString::Printf(TEXT("%s survived the round!"), *PS->GetPlayerName()), MF::NoTeam);
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
	GetWorldTimerManager().SetTimer(RoundTimer, this, &AMFGameMode_BattleRoyale::NextRound, 3.f, false);
}

void AMFGameMode_BattleRoyale::Logout(AController* Exiting)
{
	DeadThisRound.Remove(Exiting);
	Super::Logout(Exiting);
}

void AMFGameMode_BattleRoyale::NextRound()
{
	if (!IsMatchInProgress())
	{
		return;
	}
	// restartRound: clean up, cars gone (their generators bring new ones), new loot, full zone
	bRoundOver = false;
	DeadThisRound.Reset();
	ClearItems();
	for (TActorIterator<AMFVehicle> It(GetWorld()); It; ++It)
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

void AMFGameMode_BattleRoyale::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const
{
	const AMFCharacter* C = Bot ? Cast<AMFCharacter>(Bot->GetPawn()) : nullptr;
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
	for (TActorIterator<AMFBRItemBox> It(GetWorld()); It; ++It)
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
