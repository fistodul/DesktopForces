#include "Game/Race/MentalRace.h"
#include "DesktopForces.h"
#include "Game/DFGameState.h"
#include "Game/DFPlayerState.h"
#include "Player/DFCharacter.h"
#include "World/DFMapActors.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace {
	const TCHAR* RaceTeamWord(uint8 Team) {
		return Team == 1 ? TEXT("Red") : TEXT("Blue");
	}
} // namespace

// --- race car --------------------------------------------------------------------------------

ARaceCar::ARaceCar() {
	Armour = 0.74f; // MentalRaceCar
}

float ARaceCar::BotDesirability(const ADFCharacter* Bot) const {
	const float W = Super::BotDesirability(Bot);
	if (!Bot || W <= 0.f)
		return W;
	return Bot->GetTeam() == RaceTeam ? W * 4.f : -1.f; // FindSpecialAttractionForX: their own car
}

void ARaceCar::EndPlay(const EEndPlayReason::Type Reason) {
	if (Reason == EEndPlayReason::Destroyed && HasAuthority())
		if (AMentalRace* GM = GetWorld()->GetAuthGameMode<AMentalRace>())
			GM->RaceCarGone(this);
	Super::EndPlay(Reason);
}

// --- check point ---------------------------------------------------------------------------

ARaceCheckPoint::ARaceCheckPoint() {
	Radius = 864.f; // CollisionRadius 256 uu, touched within 128 uu more
	HalfHeight = 576.f; // CollisionHeight 256 uu
}

void ARaceCheckPoint::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARaceCheckPoint, NextFor);
	DOREPLIFETIME(ARaceCheckPoint, Index);
}

// --- mode ------------------------------------------------------------------------------------

AMentalRace::AMentalRace() {
	GameName = TEXT("Mental Race");
	MFGameClass = TEXT("MentalRace");
	NumTeams = 2;
	ScoreLimit = 6; // GoalTeamScore
	TimeLimit = 8;
	bScoreTeamKills = false;
	bAlwaysUseVehicles = true;
}

void AMentalRace::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) {
	Super::InitGame(MapName, Options, ErrorMessage);
	bLap = UGameplayStatics::GetIntOption(Options, TEXT("Lap"), bLap ? 1 : 0) != 0;
}

void AMentalRace::GetVehicleExclusions(TArray<FVector>& OutPoints) const {
	// the race cars stand on the parks
	for (const ADFLegacyActor* L : ADFLegacyActor::FindAll(GetWorld(), TEXT("TrailerPark")))
		OutPoints.Add(L->GetActorLocation());
}

void AMentalRace::HandleMatchHasStarted() {
	Super::HandleMatchHasStarted();
	// PreBeginPlay: a car generator and a check point at each team's trailer park, a check point
	// at the trailer generator
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	CheckPoints.Init(nullptr, 3);
	for (ADFLegacyActor* L : ADFLegacyActor::FindAll(GetWorld(), TEXT("TrailerPark"))) {
		const uint8 Team = L->Team == 1 ? 1 : 0;
		if (CheckPoints[Team])
			continue;
		const FTransform T = L->GetActorTransform();
		if (AVehicleGenerator* G = GetWorld()->SpawnActorDeferred<AVehicleGenerator>(AVehicleGenerator::StaticClass(), T)) {
			G->VehicleClass = Team == 1 ? ARaceCarRed::StaticClass() : ARaceCar::StaticClass();
			G->FinishSpawning(T);
		}
		if (ARaceCheckPoint* CP = GetWorld()->SpawnActor<ARaceCheckPoint>(T.GetLocation(), FRotator::ZeroRotator, SP)) {
			CP->Index = Team;
			CP->Init(Team);
			CheckPoints[Team] = CP;
		}
	}
	const TArray<ADFLegacyActor*> Gens = ADFLegacyActor::FindAll(GetWorld(), TEXT("TrailerGenerator"));
	if (Gens.Num()) {
		if (ARaceCheckPoint* CP = GetWorld()->SpawnActor<ARaceCheckPoint>(Gens[0]->GetActorLocation(), FRotator::ZeroRotator, SP)) {
			CP->Index = 2;
			CP->Init(DF::NoTeam);
			CheckPoints[2] = CP;
		}
	}
	if (!CheckPoints[0] || !CheckPoints[1] || !CheckPoints[2]) {
		UE_LOG(LogDF, Warning, TEXT("Mental Race: the map needs two trailer parks and a trailer generator"));
		return;
	}
	Next[0] = Next[1] = 2;
	UpdateMarks();
	GetWorldTimerManager().SetTimer(CheckTimer, this, &AMentalRace::CheckCars, 0.1f, true);
}

ARaceCheckPoint* AMentalRace::NextCheckPoint(uint8 Team) const {
	if (Team > 1 || CheckPoints.Num() < 3)
		return nullptr;
	return CheckPoints[Next[Team] == 3 ? 2 : Next[Team]];
}

void AMentalRace::UpdateMarks() {
	for (ARaceCheckPoint* CP : CheckPoints)
		if (CP)
			CP->NextFor = 0;
	for (uint8 Team = 0; Team < 2; ++Team)
		if (ARaceCheckPoint* CP = NextCheckPoint(Team))
			CP->NextFor |= 1 << Team;
}

void AMentalRace::CheckCars() {
	if (!IsMatchInProgress())
		return;
	for (TActorIterator<ARaceCar> It(GetWorld()); It; ++It) {
		const ARaceCheckPoint* CP = NextCheckPoint(It->RaceTeam);
		if (CP && It->Health > 0 && CP->Contains(It->GetActorLocation())) {
			Passed(It->RaceTeam, *It);
			if (!IsMatchInProgress())
				return;
		}
	}
}

void AMentalRace::Passed(uint8 Team, ADFVehicle* Car) {
	// ScoreCheckPoint: the driver scores (against them when racing the other team's car)
	// a driving bot / player possesses the car: its player state is the car's
	ADFCharacter* Driver = Car->GetDriver();
	ADFPlayerState* PS = Car->GetPlayerState<ADFPlayerState>();
	if (!PS && Driver)
		PS = Driver->GetPlayerState<ADFPlayerState>();
	if (PS)
		PS->SetScore(PS->GetScore() + (PS->GetTeam() == Team ? 1.f : -1.f));
	const FString Who = PS ? PS->GetPlayerName() : FString(TEXT("Someone"));
	if (Next[Team] == Team) {
		// home: a lap
		ADFGameState* GS = GetGameState<ADFGameState>();
		if (GS && GS->TeamScores.IsValidIndex(Team))
			GS->TeamScores[Team] += 1;
		Event(FString::Printf(TEXT("%s completed a lap for %s!"), *Who, RaceTeamWord(Team)), Team, Team == 1 ? TEXT("Red_score") : TEXT("Blue_score"));
		Next[Team] = 2;
		if (GS && (bOverTime || (ScoreLimit > 0 && GS->GetTeamScore(Team) >= ScoreLimit))) {
			FinishMatch(FString::Printf(TEXT("%s Team Wins!"), *GS->GetTeamName(Team).ToString()));
			return;
		}
	} else {
		Event(FString::Printf(TEXT("%s passed Check Point for %s."), *Who, RaceTeamWord(Team)), Team);
		if (Next[Team] == 2)
			Next[Team] = 1 - Team; // on to the enemy park
		else if (Next[Team] == 1 - Team)
			Next[Team] = bLap ? Team : 3; // home, or by the middle
		else
			Next[Team] = Team; // 3: home
	}
	UpdateMarks();
}

void AMentalRace::RaceCarGone(const ARaceCar* Car) {
	if (IsMatchInProgress())
		Event(FString::Printf(TEXT("%s Race Car Destroyed."), RaceTeamWord(Car->RaceTeam)), Car->RaceTeam); // MentalRaceMessage 3
}

void AMentalRace::OnTimeLimit() {
	const ADFGameState* GS = GetGameState<ADFGameState>();
	if (GS && GS->TeamScores.Num() >= 2 && GS->TeamScores[0] == GS->TeamScores[1]) {
		if (!bOverTime) {
			bOverTime = true; // the next lap wins
			Event(TEXT("Overtime: the next lap wins!"), DF::NoTeam);
		}
		return;
	}
	Super::OnTimeLimit();
}

void AMentalRace::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const {
	// FindSpecialAttractionForX: SetBotOrders Attack - their car while nobody drives it (in
	// another vehicle: driven there, then the swap), else the next check point
	const ADFPlayerState* PS = Bot ? Bot->GetPlayerState<ADFPlayerState>() : nullptr;
	if (!PS || PS->GetTeam() > 1)
		return;
	if (ADFVehicle* Car = GetBotVehicle(Bot))
		OutGoals.Add(Car);
	else if (ARaceCheckPoint* CP = NextCheckPoint(PS->GetTeam()))
		OutGoals.Add(CP);
}

AActor* AMentalRace::GetBotDriveTarget(AController* Bot) const {
	// only their own race car races; in anything else the drive goes by the road to their goal
	const ARaceCar* Car = Bot ? Cast<ARaceCar>(Bot->GetPawn()) : nullptr;
	const ADFPlayerState* PS = Bot ? Bot->GetPlayerState<ADFPlayerState>() : nullptr;
	return Car && PS && Car->RaceTeam == PS->GetTeam() ? NextCheckPoint(Car->RaceTeam) : nullptr;
}

ADFVehicle* AMentalRace::GetBotVehicle(AController* Bot) const {
	// on foot: "Go for the Car", unless a team mate drives it already
	const ADFPlayerState* PS = Bot ? Bot->GetPlayerState<ADFPlayerState>() : nullptr;
	const ADFCharacter* C = Bot ? Cast<ADFCharacter>(Bot->GetPawn()) : nullptr;
	if (!PS || !C || PS->GetTeam() > 1 || !IsMatchInProgress())
		return nullptr;
	for (TActorIterator<ARaceCar> It(GetWorld()); It; ++It)
		if (It->RaceTeam == PS->GetTeam() && !It->GetDriver() && It->BotDesirability(C) > 0.f) // not wrecked, stuck, moving
			return *It;
	return nullptr;
}
