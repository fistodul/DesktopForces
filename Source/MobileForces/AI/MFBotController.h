#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Core/MFTypes.h"
#include "MFBotController.generated.h"

class AMFCharacter;
class AMFWeapon;

/** One of MF's named bots (RageBotA..P): name, favourite loadout, preferred team. */
USTRUCT()
struct FMFBotProfile
{
	GENERATED_BODY()
	UPROPERTY() FString Name;
	UPROPERTY() TArray<TSubclassOf<AMFWeapon>> Loadout;
	UPROPERTY() EMFTeam PreferredTeam = EMFTeam::Blue;
};

/**
 * Deathmatch / team bot (stand-in for MF's RageBot until a full port).
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

	FMFBotProfile Profile;

	/** The 16 MF bots (Heywood, Coffey, Jackson ...). */
	static const TArray<FMFBotProfile>& Roster();

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
	float ReactionDue = 0.f;
	bool bHoldingFire = false;
	float FireStart = 0.f;
};
