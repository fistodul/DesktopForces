#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/DFGameMode.h"
#include "DeBall.generated.h"

class USphereComponent;
class UStaticMeshComponent;

/** The DeBall ball (MF-mods MoBall's TestMoBallProp): a big rubber ball, pushed by vehicles, soldiers and shots. */
UCLASS()
class DESKTOPFORCES_API ADeBall : public AActor
{
	GENERATED_BODY()

public:
	ADeBall();
	virtual void BeginPlay() override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void FellOutOfWorld(const UDamageType& DmgType) override;
	virtual void Destroyed() override;

	/** Scored or lost: gone after this many seconds, then a new one. */
	void Expire(float Seconds);
	bool IsExpiring() const { return bExpiring; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "DeBall")
	TObjectPtr<USphereComponent> Sphere;
	UPROPERTY(VisibleAnywhere, Category = "DeBall")
	TObjectPtr<UStaticMeshComponent> Mesh;
	bool bExpiring = false;
};

/**
 * A DeBall goal (MoBallGoal): MF drew a faint translucent ring at each trailer park; this one
 * shows the real scoring cylinder, an additive net in the team's colour (SM_MF_Ring,
 * M_MF_GoalField; Tools/artgen goalfield).
 */
UCLASS(NotPlaceable)
class DESKTOPFORCES_API ADeBallGoal : public AActor
{
	GENERATED_BODY()

public:
	ADeBallGoal();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Whose goal: the other team scores here. */
	UPROPERTY(ReplicatedUsing = OnRep_Team)
	uint8 Team = 0;
	/** The scoring cylinder around the actor: radius, half height (cm). */
	float Radius = 720.f;
	float HalfHeight = 244.f;

	bool Contains(const FVector& P) const;
	/** Server: set up after spawning (sizes the ring, from the ground up). */
	void Init(uint8 InTeam);

protected:
	UFUNCTION()
	void OnRep_Team();

	UPROPERTY(VisibleAnywhere, Category = "DeBall")
	TObjectPtr<UStaticMeshComponent> Ring;
	/** The trailer park: the middle of the scoring cylinder (server). */
	FVector Centre = FVector::ZeroVector;
};

/**
 * DeBall (MF-mods MoBall, renamed): football with vehicles. The ball appears at the map's trailer
 * generator; pushing it into the other team's goal (a trailer park) scores. Loadouts only hand
 * out knives, pistols and gadgets (groups 1, 2, 9, 10, 11). First to 4 goals; a draw at the end
 * of the 15 minutes goes to sudden death. Maps: MB-*, or any map with a trailer generator.
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API ADeBallGame : public ADFGameMode
{
	GENERATED_BODY()

public:
	ADeBallGame();
	virtual void InitGameState() override;
	virtual void HandleMatchHasStarted() override;
	virtual void GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const override;
	/** FindSpecialAttractionForX: a bot at the wheel drives at the ball. */
	virtual AActor* GetBotDriveTarget(AController* Bot) const override;

	void BallGone(ADeBall* Ball);

	/** Only MB weapons (MoBallGame.bRestrictWeapons): AllowedItemGroups. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DeBall")
	bool bRestrictWeapons = true;

	UPROPERTY(Config, EditDefaultsOnly, Category = "DeBall")
	TArray<int32> AllowedItemGroups = { 1, 2, 9, 10, 11 };

protected:
	virtual void CheckEndConditions(ADFPlayerState* LastKiller) override {}
	virtual void OnTimeLimit() override;
	void SpawnBall();
	void CheckGoals();

	UPROPERTY(Transient)
	TArray<TObjectPtr<ADeBallGoal>> Goals;
	FTransform BallStart;
	UPROPERTY(Transient)
	TObjectPtr<ADeBall> Ball;
	FTimerHandle GoalTimer;
	FTimerHandle SpawnTimer;
	bool bOverTime = false;
};
