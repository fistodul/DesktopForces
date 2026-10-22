#pragma once

#include "CoreMinimal.h"
#include "Game/KOH/KingOfTheHill.h"
#include "Holdout.generated.h"

/**
 * Holdout (MF RageDomination): King of the Hill with one point, the map's Holdout timer;
 * first team to hold it for GoalTeamScore minutes in all (6) wins, else the most time at the
 * time limit (20 min). No capture bonus.
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API AHoldout : public AKingOfTheHill
{
	GENERATED_BODY()

public:
	AHoldout();

protected:
	virtual void SpawnPoints() override;
	virtual int32 GetGoalScore() const override { return GoalTeamScore * 60; }   // minutes
};
