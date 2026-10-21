#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/MFGameMode.h"
#include "MFHoldout.generated.h"

class AMFCharacter;
class USphereComponent;
class UStaticMeshComponent;

/**
 * Capturable point (MF RageDominationTimer / KOHTimer): touching it switches it to your
 * team; the owning team banks a second of hold time per second. Event driven - the game
 * mode's 1 Hz timer does the counting, the point itself never ticks.
 */
UCLASS()
class DESKTOPFORCES_API AMFHoldoutPoint : public AActor
{
	GENERATED_BODY()

public:
	AMFHoldoutPoint();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_OwnerTeam, BlueprintReadOnly, Category = "MF")
	uint8 OwnerTeam = MF::NoTeam;

	/** Seconds held per team. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	TArray<int32> TeamTimes;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	FString PointName;

	void SetOwnerTeam(uint8 Team);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body, bool bSweep, const FHitResult& Hit);
	UFUNCTION()
	void OnRep_OwnerTeam();
	void UpdateLook();
};

/**
 * Holdout (MF RageDomination): capture the point, hold it; first team to GoalTeamScore
 * minutes of hold time wins, else most time at the time limit (20 min). Kills don't
 * score for the team. Subclasses change how many points exist and how they score (KOH).
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API AMFGameMode_Holdout : public AMFGameMode
{
	GENERATED_BODY()

public:
	AMFGameMode_Holdout();

	virtual void HandleMatchHasStarted() override;
	virtual void HandleMatchHasEnded() override;
	virtual FName GetOrderSound(int32 Order) const override;

	/** A team took a point (called by the point). */
	virtual void PointCaptured(AMFHoldoutPoint* Point, AMFCharacter* By);

	const TArray<TObjectPtr<AMFHoldoutPoint>>& GetPoints() const { return Points; }

	/** Points not held by the bot's team, nearest first (jittered so bots spread), then own ones to guard. */
	virtual void GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const override;

protected:
	/** Create the points for this match (default: at the map's Holdout timer). */
	virtual void SpawnPoints();
	/** Once per second while the match runs. */
	virtual void UpdatePoints();
	virtual void CheckEndConditions(AMFPlayerState* LastKiller) override {}
	virtual void OnTimeLimit() override;

	AMFHoldoutPoint* SpawnPointAt(const FTransform& Where, const FString& Name);

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMFHoldoutPoint>> Points;

	FTimerHandle PointTimer;
};
