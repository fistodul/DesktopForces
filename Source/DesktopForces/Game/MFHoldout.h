#pragma once

#include "CoreMinimal.h"
#include "Game/KOH/MFGameMode_KOH.h"
#include "MFHoldout.generated.h"

/**
 * Holdout (MF RageDomination): King of the Hill with one point, the map's Holdout timer;
 * first team to hold it for GoalTeamScore minutes in all (6) wins, else the most time at the
 * time limit (20 min). No capture bonus.
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API AMFGameMode_Holdout : public AMFGameMode_KOH
{
	GENERATED_BODY()

public:
	AMFGameMode_Holdout();

protected:
	virtual void SpawnPoints() override;
	virtual int32 GetGoalScore() const override { return GoalTeamScore * 60; }   // minutes
};
