#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/MFGameMode.h"
#include "MFSafecracker.generated.h"

class AMFCharacter;
class AMFGold;
class UBoxComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * A team's safe (MF RageSafe). Holds the team's gold. Only the defending team's safe can be
 * cracked: damage takes its 100 health down (explosives x0.15, anything else x0.05, at least
 * 1 per hit); below zero the door blows open. Touching the open safe takes the gold; touching
 * your own safe with the enemy gold scores. Placed by the game mode at the map's RageSafes.
 */
UCLASS()
class MOBILEFORCES_API AMFSafe : public AActor
{
	GENERATED_BODY()

public:
	AMFSafe();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	uint8 Team = 0;

	/** 100 = intact, below 0 = blown (MF RageSafe.Health). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	float Health = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_Open, BlueprintReadOnly, Category = "MF")
	bool bOpen = false;

	/** The defending team's safe: the only one that takes damage this part. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	bool bCrackable = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	TObjectPtr<AMFGold> Gold;

	/** Server: set up and spawn this safe's gold. */
	void Init(uint8 InTeam, bool bInCrackable);
	void OpenSafe();
	/** Back to closed and repaired (MF CloseSafe; only while the gold is home). */
	bool CloseSafe();
	void TouchedBy(AMFCharacter* C);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UBoxComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bSweep, const FHitResult& Hit);
	UFUNCTION()
	void OnRep_Open();
	void UpdateLook();
	FTimerHandle CloseTimer;
};

UENUM(BlueprintType)
enum class EMFGoldState : uint8
{
	Home,
	Held,
	Dropped,
};

/**
 * A team's gold (MF RageSafeObject). Taken from the open safe, carried on the holder's back,
 * dropped where the holder dies. Dropped gold goes home after 25 s, or when one of its own
 * team touches it; anyone else picks it up (the last holder only after 1 s).
 */
UCLASS()
class MOBILEFORCES_API AMFGold : public AActor
{
	GENERATED_BODY()

public:
	AMFGold();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	uint8 Team = 0;

	UPROPERTY(ReplicatedUsing = OnRep_State, BlueprintReadOnly, Category = "MF")
	EMFGoldState State = EMFGoldState::Home;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	TObjectPtr<AMFCharacter> Holder;

	TWeakObjectPtr<AMFSafe> HomeSafe;

	void SendHome();
	void PickUp(AMFCharacter* By);
	/** Holder died / left: fall to the floor where they were. */
	void Drop();

protected:
	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bSweep, const FHitResult& Hit);
	UFUNCTION()
	void OnRep_State();
	virtual void BeginPlay() override;
	void UpdateLook();

	TWeakObjectPtr<AMFCharacter> OldHolder;
	float TimeDropped = 0.f;
	FTimerHandle ReturnTimer;
};

/**
 * Safecracker (MF RageSafeCracker), two parts like UT Assault. Part 1: the attackers (red by
 * default, FirstAttackerIndex) crack the defenders' safe, grab the gold and carry it to their
 * own safe before the 9 minute limit - else the defenders "safeguarded their gold". Part 2:
 * sides swap and the new attackers must beat part 1's time. Carrier kill +4, steal +7.
 */
UCLASS(Config = Game)
class MOBILEFORCES_API AMFGameMode_Safecracker : public AMFGameMode
{
	GENERATED_BODY()

public:
	AMFGameMode_Safecracker();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void HandleMatchHasStarted() override;
	virtual void Killed(AController* Killer, AController* Victim, AMFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType) override;
	virtual void GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const override;
	virtual AActor* GetBotShootTarget(AController* Bot) const override;
	virtual FString GetOrderString(int32 Order) const override;
	virtual void RestartGame() override;

	uint8 AttackerTeam() const { return Part == 2 ? 1 - FirstAttackerIndex : FirstAttackerIndex; }
	uint8 DefenderTeam() const { return 1 - AttackerTeam(); }
	int32 GetPart() const { return Part; }

	/** Someone touched their own safe carrying gold, or returned their own dropped gold. */
	void ScoreSafe(AMFCharacter* Scorer, AMFGold* Gold);
	/** Colour-coded event line for everyone (MF RageSCMessage). */
	void Event(const FString& Text, uint8 ColourTeam);

	/** Team attacking in part 1 (MF FirstAttackerIndex; 1 = red). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 FirstAttackerIndex = 1;

	/** Seconds until a blown safe shuts and repairs itself (0 = stays open, MF AutoCloseTime). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 AutoCloseTime = 0;

protected:
	virtual void CheckEndConditions(AMFPlayerState* LastKiller) override {}
	virtual void OnTimeLimit() override;
	void SpawnSafes();

	int32 Part = 1;
	/** Seconds part 1's attackers took (0 = they failed). */
	int32 FirstCaptureTime = 0;
	bool bSafeCracked = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMFSafe>> Safes;
};
