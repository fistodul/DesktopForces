#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Core/MFTypes.h"
#include "Core/MFGameSettings.h"
#include "MFBotController.generated.h"

class AMFCharacter;
class AMFWeapon;

/**
 * Deathmatch / team bot (stand-in for MF's RageBot; behaviour ideas from the MFS bot AI:
 * hunt, take cover, reload in hiding, help team mates - see Docs/Roadmap.md).
 *
 * Thinks 5x per second on a timer (no per-frame logic beyond UE path following):
 * spawn -> take its favourite loadout -> roam the navmesh -> on seeing an enemy, face it
 * with skill-based aim error, strafe and fire; reload / switch weapons when dry.
 * Needs a navmesh (built by `mfport.py light`).
 */
UCLASS()
class MOBILEFORCES_API AMFBotController : public AAIController
{
	GENERATED_BODY()

public:
	AMFBotController();

	/** 0 = novice .. 1 = godlike. Aim error and reaction scale with it. */
	UPROPERTY(EditAnywhere, Category = "MF")
	float Skill = 0.5f;

	/** Name, loadout, preferred team (from UMFGameSettings::Bots). */
	FMFBotDef Profile;

	// IGenericTeamAgentInterface: the team lives on the player state
	virtual FGenericTeamId GetGenericTeamId() const override;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	void Think();
	void DoLoadout(AMFCharacter* C);
	AMFCharacter* FindEnemy(AMFCharacter* C) const;
	void Fight(AMFCharacter* C, AMFCharacter* Target);
	void Roam(AMFCharacter* C);

	FTimerHandle ThinkTimer;
	TWeakObjectPtr<AMFCharacter> Enemy;
	float LastSeenEnemy = -100.f;
	float NextStrafe = 0.f;
	float NextRoam = 0.f;
	TWeakObjectPtr<AActor> CurrentGoal;
	float LingerUntil = 0.f;
	/** Goals that turned out unreachable, retried after a while (paths can open, e.g. doors). */
	TMap<TWeakObjectPtr<AActor>, float> BlockedGoals;
	bool TryGoal(AActor* Goal);
	float ReactionDue = 0.f;
	bool bHoldingFire = false;
	float FireStart = 0.f;
};
