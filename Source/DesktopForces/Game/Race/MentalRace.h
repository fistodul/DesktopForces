#pragma once

#include "CoreMinimal.h"
#include "Game/DFGameMode.h"
#include "Game/MoBall/MoBall.h"
#include "Vehicles/Buggy/Buggy.h"
#include "MentalRace.generated.h"

/** A team's race car (Racing.MentalRaceCar / MentalRaceCarRed): a buggy, Armour 0.74. */
UCLASS()
class DESKTOPFORCES_API ARaceCar : public ABuggy {
	GENERATED_BODY()
public:
	ARaceCar();
	/** Its team's bots want it most; the other team's don't take it. */
	virtual float BotDesirability(const ADFCharacter* Bot) const override;
	uint8 RaceTeam = 0;

protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
};

UCLASS()
class DESKTOPFORCES_API ARaceCarRed : public ARaceCar {
	GENERATED_BODY()
public:
	ARaceCarRed() { RaceTeam = 1; }
};

/**
 * A Mental Race check point (MentalRaceCheckPoint, CollisionRadius / Height 256 uu, touched
 * within 128 uu more): the ring of a MoBall goal, in the team's colour at its park, white in the
 * middle. NextFor: the teams whose next check point it is (bit per team; the HUD marks it).
 */
UCLASS(NotPlaceable)
class DESKTOPFORCES_API ARaceCheckPoint : public AMoBallGoal {
	GENERATED_BODY()
public:
	ARaceCheckPoint();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(Replicated)
	uint8 NextFor = 0;
	/** 0 / 1: that team's park, 2: the middle (the trailer generator). */
	UPROPERTY(Replicated)
	uint8 Index = 2;
};

/**
 * Mental Race (MF-mods Racing.MentalRace, a team game on trailer maps): each team has a race car,
 * made at its trailer park. Check points: the two parks and the trailer generator. A team's car
 * drives its park -> the middle -> the enemy park -> the middle again (straight home with ?Lap=1,
 * bLap) -> home: a lap, a team point. Passing a check point gives its driver +1 (-1 driving for
 * the other team). 6 laps / 8 minutes; a draw at the end goes to whoever finishes the next lap.
 * Bots get into their car however far it is (FindSpecialAttractionForX) and race it to the next
 * check point; other vehicles don't head for check points.
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API AMentalRace : public ADFGameMode {
	GENERATED_BODY()

public:
	AMentalRace();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void HandleMatchHasStarted() override;
	virtual void GetVehicleExclusions(TArray<FVector>& OutPoints) const override;
	virtual void GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const override;
	virtual AActor* GetBotDriveTarget(AController* Bot) const override;
	virtual ADFVehicle* GetBotVehicle(AController* Bot) const override;

	/** Straight home from the enemy park, not by the middle (MentalRace.bLap). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Race")
	bool bLap = false;

	void RaceCarGone(const ARaceCar* Car);

protected:
	virtual void CheckEndConditions(ADFPlayerState* LastKiller) override {}
	virtual void OnTimeLimit() override;
	void CheckCars();
	void Passed(uint8 Team, ADFVehicle* Car);
	ARaceCheckPoint* NextCheckPoint(uint8 Team) const;
	void UpdateMarks();

	UPROPERTY(Transient)
	TArray<TObjectPtr<ARaceCheckPoint>> CheckPoints; // 0 / 1 the parks, 2 the middle
	/** Per team: 0 / 1 that park next, 2 the middle, 3 the middle on the way home. */
	int32 Next[2] = { 2, 2 };
	bool bOverTime = false;
	FTimerHandle CheckTimer;
};
