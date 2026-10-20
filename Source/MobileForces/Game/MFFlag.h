#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/MFGameMode.h"
#include "MFFlag.generated.h"

class AMFCharacter;
class USphereComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EMFFlagState : uint8
{
	Home,
	Held,
	Dropped,
};

/**
 * A team's carried objective (MF RageFlag: the CTF flag, the Safecracker gold). Lies at its
 * home, is carried on the holder's back, falls where the holder dies. Dropped, it goes home
 * after 25 s or when its own team touches it (a "return"); anyone else picks it up (the last
 * holder only after 1 s). The game mode decides what touching it at home does.
 */
UCLASS()
class MOBILEFORCES_API AMFFlag : public AActor
{
	GENERATED_BODY()

public:
	AMFFlag();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	uint8 Team = 0;

	UPROPERTY(ReplicatedUsing = OnRep_State, BlueprintReadOnly, Category = "MF")
	EMFFlagState State = EMFFlagState::Home;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	TObjectPtr<AMFCharacter> Holder;

	/** Where it goes home to (the flag base / the safe). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	TObjectPtr<AActor> HomeBase;

	/** Hidden while at home (gold inside a shut safe). */
	UPROPERTY(ReplicatedUsing = OnRep_State, BlueprintReadOnly, Category = "MF")
	bool bHiddenAtHome = false;

	void SendHome();
	void PickUp(AMFCharacter* By);
	/** Holder died / left: fall to the floor where they were. */
	void Drop();
	/** Lie at Spot, takeable, with the 25 s timeout running (the Detonation key's spawns). */
	void PlaceDropped(const FVector& Spot, const FRotator& Rot = FRotator::ZeroRotator);
	void UpdateLook();

	static AMFFlag* FindCarriedBy(const AMFCharacter* C);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bSweep, const FHitResult& Hit);
	UFUNCTION()
	void OnRep_State();

	TWeakObjectPtr<AMFCharacter> OldHolder;
	float TimeDropped = 0.f;
	FTimerHandle ReturnTimer;
};

/**
 * Shared rules of the carry-the-objective modes (MF RageCTF / RageSafeCracker): the carrier
 * drops it on death (killer +4), delivering it scores +7 and a team point, event lines in the
 * team colour ("Name: I have the Blue flag! ...").
 */
UCLASS(Abstract, Config = Game)
class MOBILEFORCES_API AMFGameMode_Flags : public AMFGameMode
{
	GENERATED_BODY()

public:
	AMFGameMode_Flags();
	virtual void Killed(AController* Killer, AController* Victim, AMFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType) override;

	/** Someone touched the flag lying at its home. */
	virtual void FlagTouchedAtHome(AMFFlag* Flag, AMFCharacter* C) {}
	/** Its own team touched it where it was dropped. */
	virtual void FlagReturned(AMFFlag* Flag, AMFCharacter* By);
	/** The carrier brought an enemy flag home: +7, a team point, the goal check. */
	virtual void FlagCaptured(AMFFlag* Flag, AMFCharacter* By);
	virtual void FlagPickedUp(AMFFlag* Flag, AMFCharacter* By);
	virtual void FlagDropped(AMFFlag* Flag, AMFCharacter* Was);
	virtual void FlagWentHome(AMFFlag* Flag);
	/** Lay untouched for 25 s after a drop: default home with a "returned" line. */
	virtual void FlagTimedOut(AMFFlag* Flag);

	static const TCHAR* TeamName(uint8 Team);
	static FString NameOf(const APawn* P);
	const FString& GetObjectName() const { return ObjectName; }

protected:
	virtual void CheckEndConditions(AMFPlayerState* LastKiller) override {}
	AMFFlag* SpawnFlag(uint8 Team, AActor* Home, const FVector& Location);

	/** "flag" / "gold" in the messages. */
	FString ObjectName = TEXT("flag");
	/** After "I have the Blue flag!" (MF HasBlue text). */
	FString CarryLine = TEXT("Returning to base!");

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMFFlag>> Flags;
};

/** Flag base (MF RageFlagBase): the flag's home; bring the enemy flag here while yours is home. */
UCLASS()
class MOBILEFORCES_API AMFFlagBase : public AActor
{
	GENERATED_BODY()

public:
	AMFFlagBase();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	uint8 Team = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	TObjectPtr<AMFFlag> Flag;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UStaticMeshComponent> Mesh;
};

/**
 * Capture the Flag (MF RageCTF): take the enemy flag from their base and touch your own
 * flag at home with it. First to 4 captures, 20 minutes, respawn after 5 s.
 */
UCLASS(Config = Game)
class MOBILEFORCES_API AMFGameMode_CTF : public AMFGameMode_Flags
{
	GENERATED_BODY()

public:
	AMFGameMode_CTF();
	virtual void HandleMatchHasStarted() override;
	virtual void FlagTouchedAtHome(AMFFlag* Flag, AMFCharacter* C) override;
	virtual void GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const override;
	virtual FString GetOrderString(int32 Order) const override;

protected:
	virtual void OnTimeLimit() override;
	UPROPERTY(Transient)
	TArray<TObjectPtr<AMFFlagBase>> Bases;
};
