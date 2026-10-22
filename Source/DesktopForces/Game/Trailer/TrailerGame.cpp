#include "Game/Trailer/TrailerGame.h"
#include "DesktopForces.h"
#include "Core/DFGameSettings.h"
#include "Game/DFGameState.h"
#include "Game/DFPlayerState.h"
#include "Player/DFCharacter.h"
#include "Vehicles/DFVehicles.h"
#include "World/DFMapActors.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "World/DFEffectActor.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace
{
	constexpr float Uu = 2.25f;
	const TCHAR* TrailerSquad(uint8 Team) { return Team == 1 ? TEXT("Red Squad") : TEXT("Blue Squad"); }
}

ATrailerGame::ATrailerGame()
{
	GameName = TEXT("Trailer");
	MFGameClass = TEXT("TrailerGame");
	InstructionSound = TEXT("Get_the_trailer");
	NumTeams = 2;
	GoalTeamScore = 4;      // TrailerGame.GoalTeamScore
	TimeLimit = 10;
	RespawnWait = 5;
	FragLimit = 0;
	bScoreTeamKills = false;
	bVehicles = true;       // bAlwaysUseVehciles
}

void ATrailerGame::FindMapData()
{
	if (bHaveGenerator || Bays.Num())
	{
		return;
	}
	for (ADFLegacyActor* L : ADFLegacyActor::FindAll(GetWorld(), TEXT("TrailerDelivery")))
	{
		const FString* R = L->Properties.Find(TEXT("CollisionRadius"));
		const FString* H = L->Properties.Find(TEXT("CollisionHeight"));
		// TrailerDelivery defaults: radius 256, height 64 uu; TestOverlap allows 128 uu more
		Bays.Add({ L->GetActorLocation(), L->Team == 1 ? uint8(1) : uint8(0), (R ? FCString::Atof(**R) : 256.f) * Uu,
			(H ? FCString::Atof(**H) : 64.f) * Uu });
	}
	const TArray<ADFLegacyActor*> Gens = ADFLegacyActor::FindAll(GetWorld(), TEXT("TrailerGenerator"));
	if (Gens.Num())
	{
		GeneratorSpot = Gens[0]->GetActorTransform();
		bHaveGenerator = true;
	}
}

void ATrailerGame::GetVehicleExclusions(TArray<FVector>& OutPoints) const
{
	const_cast<ATrailerGame*>(this)->FindMapData();
	for (const FBay& B : Bays)
	{
		OutPoints.Add(B.Location);
	}
}

void ATrailerGame::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();
	FindMapData();
	if (!bHaveGenerator || Bays.Num() < 2)
	{
		UE_LOG(LogDF, Warning, TEXT("Trailer: map has %s generator and %d delivery bays"), bHaveGenerator ? TEXT("a") : TEXT("no"), Bays.Num());
		return;
	}
	SpawnTrailer();
	GetWorldTimerManager().SetTimer(CheckTimer, this, &ATrailerGame::CheckDelivery, 0.25f, true);
}

void ATrailerGame::SpawnTrailer()
{
	DeliveredTo = -1;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	// TrailerGenerator.CreateVehicle: cab 10 uu up, trailer (-244, 0, 10) uu behind it
	const FRotator Rot(0.f, GeneratorSpot.Rotator().Yaw, 0.f);
	const FVector At = GeneratorSpot.GetLocation();
	Cab = GetWorld()->SpawnActor<ADFVehicle>(ATrailerCab::StaticClass(), At + FVector(0.f, 0.f, 150.f), Rot, SP);
	TrailerVehicle = GetWorld()->SpawnActor<ADFVehicle>(ATrailer::StaticClass(), At + Rot.RotateVector(FVector(-244.f * Uu, 0.f, 0.f)) + FVector(0.f, 0.f, 150.f), Rot, SP);
	if (!Cab || !TrailerVehicle)
	{
		UE_LOG(LogDF, Warning, TEXT("Trailer: could not place the cab / trailer"));
		return;
	}
	Cab->HitchTrailer(TrailerVehicle, FVector(-300.f, 0.f, 40.f));
	Event(TEXT("Trailer has restarted"), DF::NoTeam);
}

void ATrailerGame::VehicleEntered(ADFVehicle* Vehicle, ADFCharacter* Driver)
{
	if (Vehicle == Cab && Driver)
	{
		Event(FString::Printf(TEXT("%s: trailer has been captured! Driving to enemy base!"),
			Driver->GetPlayerState() ? *Driver->GetPlayerState()->GetPlayerName() : TEXT("Someone")), Driver->GetTeam(), TEXT("Trailer_capture"));   // RageTrailerMessage 4
	}
}

void ATrailerGame::CheckDelivery()
{
	if (!IsMatchInProgress() || DeliveredTo >= 0)
	{
		return;
	}
	if (!IsValid(Cab) || !IsValid(TrailerVehicle))
	{
		// blown up on the way: a fresh one 5 s later
		if (!GetWorldTimerManager().IsTimerActive(RespawnTimer))
		{
			if (IsValid(Cab)) { Cab->Destroy(); }
			if (IsValid(TrailerVehicle)) { TrailerVehicle->Destroy(); }
			Cab = nullptr;
			TrailerVehicle = nullptr;
			GetWorldTimerManager().SetTimer(RespawnTimer, this, &ATrailerGame::SpawnTrailer, 5.f, false);
		}
		return;
	}
	ADFCharacter* By = Cab->GetDriver() ? Cab->GetDriver() : Cab->GetLastDriver();
	if (!By || By->GetTeam() > 1)
	{
		return;
	}
	// TrailerDelivery.TestOverlap: within radius + 128 uu flat, within the height
	for (const FBay& Bay : Bays)
	{
		if (Bay.Team == By->GetTeam())
		{
			continue;
		}
		for (const ADFVehicle* V : { Cab.Get(), TrailerVehicle.Get() })
		{
			const FVector D = V->GetActorLocation() - Bay.Location;
			if (FMath::Abs(D.Z) < Bay.Height + 150.f && D.Size2D() - 128.f * Uu < Bay.Radius)
			{
				DeliveredTo = Bay.Team;
				// the driver's controller sits on the cab while they drive
				Scorer = Cab->GetDriver() ? Cab->GetController() : By->GetController();
				Cab->LockDown();
				Event(FString::Printf(TEXT("Trailer delivered to %s"), TrailerSquad(Bay.Team)), 1 - Bay.Team, TEXT("trailer_delivered_explosion_imminent"));
				UGameplayStatics::PlaySoundAtLocation(this, LoadObject<USoundBase>(nullptr, TEXT("/MobileForcesClassic/Sounds/TrailerSFX/LockDown.LockDown"), nullptr, LOAD_NoWarn | LOAD_Quiet), Cab->GetActorLocation());
				Count = 5;
				CountDown();
				return;
			}
		}
	}
}

void ATrailerGame::CountDown()
{
	if (Count <= 0)
	{
		Detonate();
		return;
	}
	// the seconds after the first (that one is "trailer delivered, explosion imminent")
	Event(FString::Printf(TEXT("Trailer detonation in %d sec"), Count), 1 - DeliveredTo, Count < 5 ? FName(*FString::FromInt(Count)) : NAME_None);
	--Count;
	GetWorldTimerManager().SetTimer(CountTimer, this, &ATrailerGame::CountDown, 1.f, false);
}

void ATrailerGame::Detonate()
{
	const uint8 Bay = uint8(DeliveredTo);
	const uint8 ScoreTeam = 1 - Bay;
	AController* By = Scorer.Get();
	if (IsValid(TrailerVehicle))
	{
		// BombTrailer: ExplosionDamage 600, ExplosionRadius 1200 uu
		TArray<AActor*> Ignore;
		UGameplayStatics::ApplyRadialDamageWithFalloff(this, 600.f, 0.f, TrailerVehicle->GetActorLocation(), 0.f, 1200.f * Uu, 1.f,
			UDFDamage_Explosion::StaticClass(), Ignore, TrailerVehicle, By, ECC_Visibility);
		// Vehicle.BlowUp's EffectsFix.VehExplosion, seen and heard by everyone
		ADFEffectActor::Spawn(GetWorld(), NAME_None, TrailerVehicle->GetActorLocation(), 1.f, true, TEXT("Sounds/MiscSFX/Explode"), 2000.f);
	}
	// TrailerGame.ScoreDelivery: team point, +7 for the driver (-7 if they had switched sides)
	ADFGameState* GS = GetGameState<ADFGameState>();
	if (GS && GS->TeamScores.IsValidIndex(ScoreTeam))
	{
		GS->TeamScores[ScoreTeam] += 1;
	}
	ADFPlayerState* PS = By ? By->GetPlayerState<ADFPlayerState>() : nullptr;
	if (PS)
	{
		PS->SetScore(PS->GetScore() + (PS->GetTeam() == ScoreTeam ? 7.f : -7.f));
	}
	Event(FString::Printf(TEXT("%s: %s OBJECTIVE ACHIEVED! - %s BASE DESTROYED!!!"), PS ? *PS->GetPlayerName() : TrailerSquad(ScoreTeam),
		ScoreTeam == 0 ? TEXT("BLUE") : TEXT("RED"), Bay == 0 ? TEXT("BLUE") : TEXT("RED")), ScoreTeam);
	if (IsValid(Cab))
	{
		Cab->Destroy();   // takes the trailer along
	}
	Cab = nullptr;
	TrailerVehicle = nullptr;
	if (GS && GoalTeamScore > 0 && GS->GetTeamScore(ScoreTeam) >= GoalTeamScore)
	{
		FinishMatch(FString::Printf(TEXT("%s wins!"), TrailerSquad(ScoreTeam)));
		return;
	}
	GetWorldTimerManager().SetTimer(RespawnTimer, this, &ATrailerGame::SpawnTrailer, 5.f, false);
}

FString ATrailerGame::GetOrderString(int32 Order) const
{
	// Rage.int OrderString 13 / 14
	if (Order == 0)
	{
		return TEXT("Defend the command post.");
	}
	if (Order == 2)
	{
		return TEXT("Take the trailer.");
	}
	return Super::GetOrderString(Order);
}

void ATrailerGame::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const
{
	// bots don't drive yet: they go for the cab (to stop an enemy driver or escort their own)
	const ADFPlayerState* PS = Bot ? Bot->GetPlayerState<ADFPlayerState>() : nullptr;
	if (!PS || !IsValid(Cab))
	{
		return;
	}
	OutGoals.Add(Cab);
}

FName ATrailerGame::GetOrderSound(int32 Order) const
{
	// RageVoicePack.SetOrderMessage: OrderSound 13 / 14
	return Order == 0 ? FName(TEXT("Defend_the_command_post")) : Order == 2 ? FName(TEXT("Take_The_Trailer")) : Super::GetOrderSound(Order);
}
