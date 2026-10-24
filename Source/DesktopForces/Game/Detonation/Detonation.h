#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/CTF/Flag.h"
#include "Detonation.generated.h"

class ADFCharacter;
class USphereComponent;
class UStaticMeshComponent;

/**
 * A team's detonation lock (MF RageDetonationLock). An enemy carrying the key touches it: the
 * key goes in, a 5 s countdown runs, then the lock's team dies one by one (random 0-1 s
 * apart) until nobody of it is alive; that scores and the key restarts somewhere else.
 */
UCLASS()
class DESKTOPFORCES_API ADetonationLock : public AActor {
	GENERATED_BODY()

public:
	ADetonationLock();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	uint8 Team = 0;

	/** Seconds left before the detonation (-1 = not armed). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	int32 CountDown = -1;

	bool IsArmed() const { return CountDown >= 0; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category = "DF")
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
class DESKTOPFORCES_API ADetonation : public AFlagGame {
	GENERATED_BODY()

public:
	ADetonation();
	virtual void HandleMatchHasStarted() override;
	virtual void FlagTimedOut(AFlag* Flag) override;
	virtual void FlagPickedUp(AFlag* Flag, ADFCharacter* By) override;
	virtual void FlagDropped(AFlag* Flag, ADFCharacter* Was) override;
	virtual void GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const override;
	virtual FString GetOrderString(int32 Order) const override;

	/** Put the key at a random key spot (MF RageDetonationKey.Restart). */
	void RestartKey();
	/** The key went into a lock: it waits there until the detonation is over. */
	void KeyInserted(ADetonationLock* Lock, ADFCharacter* By);
	void Detonated(AController* Scorer, uint8 ScorerTeam);
	AFlag* GetKey() const { return Key; }

protected:
	UPROPERTY(Transient)
	TObjectPtr<AFlag> Key;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ADetonationLock>> Locks;

	TArray<FTransform> KeySpots;
};
