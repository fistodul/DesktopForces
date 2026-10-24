#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/DFGameMode.h"
#include "KingOfTheHill.generated.h"

class ADFCharacter;
class USphereComponent;
class UStaticMeshComponent;

/**
 * Capturable point (MF RageDominationTimer / KOHTimer): touching it switches it to your
 * team; the owning team banks a second of hold time per second. Event driven - the game
 * mode's 1 Hz timer does the counting, the point itself never ticks.
 */
UCLASS()
class DESKTOPFORCES_API AHoldoutPoint : public AActor {
	GENERATED_BODY()

public:
	AHoldoutPoint();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_OwnerTeam, BlueprintReadOnly, Category = "DF")
	uint8 OwnerTeam = DF::NoTeam;

	/** Seconds held per team. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	TArray<int32> TeamTimes;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	FString PointName;

	void SetOwnerTeam(uint8 Team);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body, bool bSweep, const FHitResult& Hit);
	UFUNCTION()
	void OnRep_OwnerTeam();
	void UpdateLook();
};

/**
 * King of the Hill (MF-mods KOH, the successor of MF's Holdout): PointsToSpawn capture points
 * chosen from the map's objective spots (Holdout timer, detonation locks, key spots, neutral
 * spawns away from the bases). Every point held adds 1 to the team score per second; taking
 * one gives the player CaptureBonus frags. First team to the goal wins, else the highest score
 * at the time limit. Kills don't score for the team. Holdout (one point, 6:00) and Arena build
 * on it.
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API AKingOfTheHill : public ADFGameMode {
	GENERATED_BODY()

public:
	AKingOfTheHill();

	virtual void HandleMatchHasStarted() override;
	virtual void HandleMatchHasEnded() override;
	virtual FName GetOrderSound(int32 Order) const override;

	/** A team took a point (called by the point). */
	virtual void PointCaptured(AHoldoutPoint* Point, ADFCharacter* By);

	/** Points not held by the bot's team, nearest first (jittered so bots spread), then own ones to guard. */
	virtual void GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const override;

	/** Capture points per match (MF: 1..6; no upper limit here). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "KOH")
	int32 PointsToSpawn = 4;

	/** Centre point + one near each base first, then spread out (else purely random). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "KOH")
	bool bCentralizedSpawning = true;

	/** Minimum spacing between points (MF MinDistance 600 uu). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "KOH")
	float MinDistance = 1350.f;

	/** Frags for taking a point (KOHGame.ScoreTimer +2). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "KOH")
	int32 CaptureBonus = 2;

protected:
	/** Create the points for this match: any placed in the map, else PointsToSpawn picked from its objective spots. */
	virtual void SpawnPoints();
	/** Once per second while the match runs: the holders score. */
	virtual void UpdatePoints();
	/** Team score that wins (GoalTeamScore; Holdout counts it in minutes). */
	virtual int32 GetGoalScore() const { return GoalTeamScore; }
	virtual void CheckEndConditions(ADFPlayerState* LastKiller) override {}

	AHoldoutPoint* SpawnPointAt(const FTransform& Where, const FString& Name);

	struct FCandidate {
		FTransform Where;
		int32 Type;
	}; // 0 central, 1 base-side, 2 other
	bool PickPoint(TArray<FCandidate>& Pool, TArray<FTransform>& Chosen, const FVector* Ref, int32 PreferredType, float MinDist) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AHoldoutPoint>> Points;

	FTimerHandle PointTimer;
};
