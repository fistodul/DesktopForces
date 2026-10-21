#pragma once

#include "CoreMinimal.h"
#include "Game/MFHoldout.h"
#include "MFGameMode_Arena.generated.h"

/**
 * Arena (MF-mods arenaMode by Gregaras, after Team Fortress 2's Arena, suggested by Akvari):
 * rounds without respawning. A round goes to the team that wipes out the other or holds the
 * map's Holdout point for TimeUntilCapture seconds once it unlocks (TimeUntilUnlock after the
 * round starts); stepping off it, or an enemy stepping on, resets the capture. First to
 * GoalTeamScore rounds (5) wins. Maps: ARENA-*.
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API AMFGameMode_Arena : public AMFGameMode_Holdout
{
	GENERATED_BODY()

public:
	AMFGameMode_Arena();
	virtual void HandleMatchHasStarted() override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Killed(AController* Killer, AController* Victim, AMFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType) override;
	virtual void Logout(AController* Exiting) override;
	virtual void PointCaptured(AMFHoldoutPoint* Point, AMFCharacter* By) override {}

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	int32 TimeUntilUnlock = 50;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	int32 TimeUntilCapture = 10;

	/** No respawning within a round (MF disableRespawn). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	bool bDisableRespawn = true;

protected:
	virtual void UpdatePoints() override;
	virtual void OnTimeLimit() override;
	void RoundEnd(uint8 Winner, bool bCaptured);
	void NextRound();
	int32 AliveCount(uint8 Team) const;

	TSet<TWeakObjectPtr<AController>> DeadThisRound;
	int32 UnlockLeft = 50;
	uint8 CapturingTeam = MF::NoTeam;
	int32 CaptureProgress = 0;
	bool bRoundOver = false;
	bool bRestartingRound = false;
	float LastAnnounce = -100.f;
	FTimerHandle RoundTimer;
};
