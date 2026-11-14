#pragma once

#include "CoreMinimal.h"
#include "World/DFEvents.h"
#include "DFMover.generated.h"

class UStaticMeshComponent;
class URotatingMovementComponent;

/**
 * Engine.Mover: a brush outside the BSP. MF maps use them as solid scenery (no key frames),
 * spinning parts (PHYS_Rotating + RotationRate: windmills, fans, circling birds) and a few
 * doors / lifts that move between KeyPos / KeyRot over MoveTime: BumpOpenTimed and
 * StandOpenTimed open when a soldier bumps / stands on them and close StayOpenTime later;
 * TriggerOpenTimed when another actor's event names it (its Tag), TriggerToggle opens or closes
 * on each, TriggerControl opens and stays (nothing here untriggers it); bDamageTriggered ones
 * trigger themselves when hit hard enough (glass panes). Fully open, it fires its own Event
 * (Mover.FinishedOpening: glass breaking, chained doors).
 * Like every UE1 actor it only exists in the game types GameInfo.IsRelevant lets through
 * (bTeamPlay, bNonTeamPlay, CondGameType). Placed by `mfport.py import --steps patch`.
 */
UCLASS()
class DESKTOPFORCES_API ADFMover : public ADFEventActor {
	GENERATED_BODY()

public:
	ADFMover();

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<URotatingMovementComponent> Spin;

	/** Key frames relative to where it was placed (KeyPos / KeyRot; key 0 is usually zero). */
	UPROPERTY(EditAnywhere, Category = "DF")
	TArray<FVector> KeyPos;
	UPROPERTY(EditAnywhere, Category = "DF")
	TArray<FRotator> KeyRot;

	UPROPERTY(EditAnywhere, Category = "DF")
	float MoveTime = 1.f;
	UPROPERTY(EditAnywhere, Category = "DF")
	float StayOpenTime = 4.f;

	/** UE1 state name: BumpOpenTimed (default), StandOpenTimed, TriggerOpenTimed, None ... */
	UPROPERTY(EditAnywhere, Category = "DF")
	FName InitialState = TEXT("BumpOpenTimed");

	/** GameInfo.IsRelevant: only in this MF game class ("RageCTF" ...; none: any). */
	UPROPERTY(EditAnywhere, Category = "DF")
	FName CondGameType;
	UPROPERTY(EditAnywhere, Category = "DF")
	bool bTeamPlay = true;
	UPROPERTY(EditAnywhere, Category = "DF")
	bool bNonTeamPlay = true;

	/** Mover.bDamageTriggered: a hit of DamageThreshold or more triggers it. */
	UPROPERTY(EditAnywhere, Category = "DF")
	bool bDamageTriggered = false;
	UPROPERTY(EditAnywhere, Category = "DF")
	float DamageThreshold = 0.f;
	/** bTriggerOnceOnly: the first trigger only. */
	UPROPERTY(EditAnywhere, Category = "DF")
	bool bTriggerOnceOnly = false;

	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation,
		FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Opening (1) / closing (-1) / still (0); Alpha 0 = key 0, 1 = key 1. */
	UPROPERTY(ReplicatedUsing = OnRep_Moving)
	int8 Moving = 0;
	UFUNCTION()
	void OnRep_Moving() { SetActorTickEnabled(Moving != 0); }
	UPROPERTY(Replicated)
	float Alpha = 0.f;
	FVector BasePos;
	FRotator BaseRot;
	FTimerHandle CloseTimer;
	/** Who opened it last: whose doing its Event is. */
	TWeakObjectPtr<APawn> OpenedBy;
	bool bWasTriggered = false;
	virtual void Triggered(AActor* Other, APawn* By) override;
	void Open();
	void Close();
};
