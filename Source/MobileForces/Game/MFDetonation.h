#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/MFFlag.h"
#include "MFDetonation.generated.h"

class AMFCharacter;
class USphereComponent;
class UStaticMeshComponent;

/**
 * A team's detonation lock (MF RageDetonationLock). An enemy carrying the key touches it: the
 * key goes in, a 5 s countdown runs, then the lock's team dies one by one (random 0-1 s
 * apart) until nobody of it is alive; that scores and the key restarts somewhere else.
 */
UCLASS()
class MOBILEFORCES_API AMFDetonationLock : public AActor
{
	GENERATED_BODY()

public:
	AMFDetonationLock();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	uint8 Team = 0;

	/** Seconds left before the detonation (-1 = not armed). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	int32 CountDown = -1;

	bool IsArmed() const { return CountDown >= 0; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bSweep, const FHitResult& Hit);
	void Tick1();

	TWeakObjectPtr<AController> Scorer;
	uint8 ScorerTeam = 0;
	bool bKilling = false;
	FTimerHandle Timer;
};

/**
 * Detonation (MF RageDetonation): one neutral key appears at a random RageDetPossibleKeyPos;
 * take it to the enemy's lock. 4 detonations / 10 minutes, respawn 5 s, carrier kill +4,
 * detonation +7. An untouched key moves to another spot after 25 s.
 */
UCLASS(Config = Game)
class MOBILEFORCES_API AMFGameMode_Detonation : public AMFGameMode_Flags
{
	GENERATED_BODY()

public:
	AMFGameMode_Detonation();
	virtual void HandleMatchHasStarted() override;
	virtual void FlagTimedOut(AMFFlag* Flag) override;
	virtual void FlagPickedUp(AMFFlag* Flag, AMFCharacter* By) override;
	virtual void FlagDropped(AMFFlag* Flag, AMFCharacter* Was) override;
	virtual void GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const override;
	virtual FString GetOrderString(int32 Order) const override;

	/** Put the key at a random key spot (MF RageDetonationKey.Restart). */
	void RestartKey();
	/** The key went into a lock: it waits there until the detonation is over. */
	void KeyInserted(AMFDetonationLock* Lock, AMFCharacter* By);
	void Detonated(AController* Scorer, uint8 ScorerTeam);
	AMFFlag* GetKey() const { return Key; }

protected:
	UPROPERTY(Transient)
	TObjectPtr<AMFFlag> Key;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMFDetonationLock>> Locks;

	TArray<FTransform> KeySpots;
};
