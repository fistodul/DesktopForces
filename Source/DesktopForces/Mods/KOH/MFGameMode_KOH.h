#pragma once

#include "CoreMinimal.h"
#include "Game/MFHoldout.h"
#include "MFGameMode_KOH.generated.h"

/**
 * King of the Hill (MF-mods KOH): PointsToSpawn capture points chosen from the map's
 * objective spots (Holdout timer, detonation locks, key spots, neutral spawns away from the
 * bases). Every point held adds 1 to the team score per second; capturing gives +2 frags.
 * First team to GoalTeamScore (700) wins. Only the selection and scoring differ from
 * Holdout - everything else is inherited.
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API AMFGameMode_KOH : public AMFGameMode_Holdout
{
	GENERATED_BODY()

public:
	AMFGameMode_KOH();

	virtual void PointCaptured(AMFHoldoutPoint* Point, AMFCharacter* By) override;

	/** Capture points per match (MF: 1..6; no upper limit here). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "KOH")
	int32 PointsToSpawn = 4;

	/** Centre point + one near each base first, then spread out (else purely random). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "KOH")
	bool bCentralizedSpawning = true;

	/** Minimum spacing between points (MF MinDistance 600 uu). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "KOH")
	float MinDistance = 1350.f;

protected:
	virtual void SpawnPoints() override;
	virtual void UpdatePoints() override;

	struct FCandidate { FTransform Where; int32 Type; };   // 0 central, 1 base-side, 2 other
	bool PickPoint(TArray<FCandidate>& Pool, TArray<FTransform>& Chosen, const FVector* Ref, int32 PreferredType, float MinDist) const;
};
