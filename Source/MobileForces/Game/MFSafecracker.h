#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/MFFlag.h"
#include "MFSafecracker.generated.h"

class AMFCharacter;
class UBoxComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * A team's safe (MF RageSafe). Holds the team's gold (an AMFFlag hidden inside). Only the
 * defending team's safe can be cracked: damage takes its 100 health down (explosives x0.15,
 * anything else x0.05, at least 1 per hit); below zero the door blows open. Touching the open
 * safe takes the gold; touching your own safe with the enemy gold scores.
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
	TObjectPtr<AMFFlag> Gold;

	/** Server: set up (Gold already spawned). */
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

/**
 * Safecracker (MF RageSafeCracker), two parts like UT Assault. Part 1: the attackers (red by
 * default, FirstAttackerIndex) crack the defenders' safe, grab the gold and carry it to their
 * own safe before the 9 minute limit - else the defenders "safeguarded their gold". Part 2:
 * sides swap and the new attackers must beat part 1's time. Carrier kill +4, steal +7.
 */
UCLASS(Config = Game)
class MOBILEFORCES_API AMFGameMode_Safecracker : public AMFGameMode_Flags
{
	GENERATED_BODY()

public:
	AMFGameMode_Safecracker();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void HandleMatchHasStarted() override;
	virtual void GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const override;
	virtual AActor* GetBotShootTarget(AController* Bot) const override;
	virtual FString GetOrderString(int32 Order) const override;
	virtual void RestartGame() override;
	virtual void FlagTouchedAtHome(AMFFlag* Flag, AMFCharacter* C) override;
	virtual void FlagCaptured(AMFFlag* Flag, AMFCharacter* By) override;

	uint8 AttackerTeam() const { return Part == 2 ? 1 - FirstAttackerIndex : FirstAttackerIndex; }
	uint8 DefenderTeam() const { return 1 - AttackerTeam(); }
	int32 GetPart() const { return Part; }

	/** Team attacking in part 1 (MF FirstAttackerIndex; 1 = red). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 FirstAttackerIndex = 1;

	/** Seconds until a blown safe shuts and repairs itself (0 = stays open, MF AutoCloseTime). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 AutoCloseTime = 0;

protected:
	virtual void OnTimeLimit() override;
	void SpawnSafes();

	int32 Part = 1;
	/** Seconds part 1's attackers took (0 = they failed). */
	int32 FirstCaptureTime = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMFSafe>> Safes;
};
