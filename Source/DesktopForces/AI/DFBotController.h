#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Core/DFTypes.h"
#include "Core/DFGameSettings.h"
#include "DFBotController.generated.h"

class ADFCharacter;
class ADFWeapon;
class ADFVehicle;

/**
 * Deathmatch / team bot (stand-in for MF's RageBot; behaviour ideas from the MFS bot AI:
 * hunt, take cover, reload in hiding, help team mates - see Docs/Roadmap.md).
 *
 * Thinks 5x per second on a timer (no per-frame logic beyond UE path following):
 * spawn -> take its favourite loadout -> roam the navmesh -> on seeing an enemy, face it
 * with skill-based aim error, strafe and fire; reload / switch weapons when dry.
 * Needs a navmesh (built by `mfport.py light`).
 *
 * Vehicles (RageBot PickVehcicle / GotoPlaceVehicle): for a goal more than 10 road nodes
 * away a bot walks to the most wanted free vehicle nearby and drives the map's road graph
 * (ADFRoadNetwork, MF's R_ROAD reach specs) to the node nearest the goal, then gets out.
 * Riding as a passenger it fights from its seat (the gunner seat aims the turret).
 */
UCLASS()
class DESKTOPFORCES_API ADFBotController : public AAIController {
	GENERATED_BODY()

public:
	ADFBotController();

	/** 0 = novice .. 1 = godlike. Aim error and reaction scale with it. */
	UPROPERTY(EditAnywhere, Category = "DF")
	float Skill = 0.5f;

	/** Team order (MF orders menu / RageBot SetOrders). */
	void SetOrder(EDFBotOrder NewOrder, APawn* Giver);
	EDFBotOrder GetOrder() const { return Order; }

	/** Name, loadout, preferred team (from UDFGameSettings::Bots). */
	FDFBotDef Profile;

	/** Placed by the mapper (ADFBotSpot): no respawn once killed. */
	bool bPlaced = false;
	/** GroundSpeed 0: holds its spot, only turns and shoots. */
	bool bStationary = false;

	// IGenericTeamAgentInterface: the team lives on the player state
	virtual FGenericTeamId GetGenericTeamId() const override;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	void Think();
	void DoLoadout(ADFCharacter* C);
	/** Took its kit since spawning (a mode may allow none of it: knife only, still fights). */
	bool bDidLoadout = false;
	ADFCharacter* FindEnemy(ADFCharacter* C) const;
	/** Target in sight of C, the cars they sit in not counting as cover. */
	bool CanSee(const ADFCharacter* C, const ADFCharacter* Target) const;
	void Fight(ADFCharacter* C, ADFCharacter* Target);
	void Roam(ADFCharacter* C);

	FTimerHandle ThinkTimer;
	EDFBotOrder Order = EDFBotOrder::Freelance;
	TWeakObjectPtr<APawn> OrderGiver;
	FVector OrderSpot = FVector::ZeroVector;
	/** Hold / Follow: move there instead of roaming. True if the order decided the move. */
	bool FollowOrder(ADFCharacter* C);
	/** The game mode's non-player target (GetBotShootTarget), if any. */
	AActor* GetObjectiveTarget() const;

	TWeakObjectPtr<ADFCharacter> Enemy;
	float LastSeenEnemy = -100.f;
	float NextStrafe = 0.f;
	float NextRoam = 0.f;
	TWeakObjectPtr<AActor> CurrentGoal;
	float LingerUntil = 0.f;
	/** Walking straight at the current goal for the last few metres. */
	bool bDirectApproach = false;
	/** Unstick: jump and re-plan when a move makes no progress (props, railings). */
	FVector StuckCheckPos = FVector::ZeroVector;
	float StuckCheckTime = 0.f;
	int32 StuckCount = 0;
	void CheckStuck(ADFCharacter* C);
	/** Goals that turned out unreachable, retried after a while (paths can open, e.g. doors). */
	TMap<TWeakObjectPtr<AActor>, float> BlockedGoals;
	bool TryGoal(AActor* Goal);
	float ReactionDue = 0.f;
	bool bHoldingFire = false;
	float FireStart = 0.f;

	// --- vehicles ------------------------------------------------------------------------
	/** The soldier this bot plays (still ours while we drive and possess the vehicle). */
	TWeakObjectPtr<ADFCharacter> Soldier;
	/** Takes vehicles for long trips (RageBot.bUseVehicles; SetBehaviour picks it per life). */
	bool bUseVehicles = true;
	void DecideVehicleUse(const ADFCharacter* C);
	/** On foot, about to travel to Dest: walk to a vehicle worth taking (PickVehcicle). */
	bool PickVehicle(ADFCharacter* C, AActor* Goal, const FVector& Dest);
	/** Walking to GoingForVehicle; gets in once near. False when that plan is off. */
	bool GoToVehicle(ADFCharacter* C);
	/** Sitting in a passenger seat: fight from it, get out when the ride is over. True while riding. */
	bool Ride(ADFCharacter* C, ADFVehicle* V);
	/** Driving: route upkeep, arrival, stuck checks (5 / s); Steer runs every frame. */
	void Drive(ADFVehicle* V);
	void Steer(ADFVehicle* V);
	void LeaveVehicle(ADFVehicle* V, const TCHAR* Why);
	TWeakObjectPtr<ADFVehicle> GoingForVehicle;
	float GoingForVehicleSince = 0.f;
	float DontEnterVehicleTime = 0.f;
	/** Where the drive goes (the goal actor if any, it may move) and the road there. */
	TWeakObjectPtr<AActor> DriveGoal;
	FVector DriveDest = FVector::ZeroVector;
	TArray<int32> Route;
	int32 RouteStep = 0;
	/** The mode's drive target (DeBall ball): chased straight, no road, no getting out. */
	TWeakObjectPtr<AActor> ChaseTarget;
	/** Stuck check and backing off, shared by both ways of driving. */
	void CheckDriveStuck(ADFVehicle* V);
	float DriveStart = 0.f;
	float SlowSince = -1.f;
	float ReverseUntil = 0.f;
	int32 DriveStuckCount = 0;
};
