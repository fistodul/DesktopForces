#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/MFGameMode.h"
#include "MFMoBall.generated.h"

class USphereComponent;
class UStaticMeshComponent;

/** The MoBall ball (TestMoBallProp): a big rubber ball, pushed by vehicles, soldiers and shots. */
UCLASS()
class DESKTOPFORCES_API AMFMoBallBall : public AActor
{
	GENERATED_BODY()

public:
	AMFMoBallBall();
	virtual void BeginPlay() override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void FellOutOfWorld(const UDamageType& DmgType) override;
	virtual void Destroyed() override;

	/** Scored or lost: gone after this many seconds, then a new one. */
	void Expire(float Seconds);
	bool IsExpiring() const { return bExpiring; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "MoBall")
	TObjectPtr<USphereComponent> Sphere;
	UPROPERTY(VisibleAnywhere, Category = "MoBall")
	TObjectPtr<UStaticMeshComponent> Mesh;
	bool bExpiring = false;
};

/**
 * MoBall (MF-mods MoBall): football with vehicles. The ball appears at the map's trailer
 * generator; pushing it into the other team's goal (a trailer park) scores. Loadouts only hand
 * out knives, pistols and gadgets (groups 1, 2, 9, 10, 11). First to 4 goals; a draw at the end
 * of the 15 minutes goes to sudden death. Maps: MB-*, or any map with a trailer generator.
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API AMFGameMode_MoBall : public AMFGameMode
{
	GENERATED_BODY()

public:
	AMFGameMode_MoBall();
	virtual void InitGameState() override;
	virtual void HandleMatchHasStarted() override;
	virtual void GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const override;

	void BallGone(AMFMoBallBall* Ball);

	/** Only MB weapons (MoBallGame.bRestrictWeapons): AllowedItemGroups. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MoBall")
	bool bRestrictWeapons = true;

	UPROPERTY(Config, EditDefaultsOnly, Category = "MoBall")
	TArray<int32> AllowedItemGroups = { 1, 2, 9, 10, 11 };

protected:
	virtual void CheckEndConditions(AMFPlayerState* LastKiller) override {}
	virtual void OnTimeLimit() override;
	void SpawnBall();
	void CheckGoals();

	struct FGoal { FVector Location; uint8 Team; };
	TArray<FGoal> Goals;
	FTransform BallStart;
	UPROPERTY(Transient)
	TObjectPtr<AMFMoBallBall> Ball;
	FTimerHandle GoalTimer;
	FTimerHandle SpawnTimer;
	bool bOverTime = false;
};
