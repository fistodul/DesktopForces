#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MFMover.generated.h"

class UStaticMeshComponent;
class URotatingMovementComponent;

/**
 * Engine.Mover: a brush outside the BSP. MF maps use them as solid scenery (no key frames),
 * spinning parts (PHYS_Rotating + RotationRate: windmills, fans, circling birds) and a few
 * doors / lifts that move between KeyPos / KeyRot over MoveTime: BumpOpenTimed and
 * StandOpenTimed open when a soldier bumps / stands on them and close StayOpenTime later.
 * Like every UE1 actor it only exists in the game types GameInfo.IsRelevant lets through
 * (bTeamPlay, bNonTeamPlay, CondGameType). Placed by `mfport.py import --steps patch`.
 */
UCLASS()
class DESKTOPFORCES_API AMFMover : public AActor
{
	GENERATED_BODY()

public:
	AMFMover();

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<URotatingMovementComponent> Spin;

	/** Key frames relative to where it was placed (KeyPos / KeyRot; key 0 is usually zero). */
	UPROPERTY(EditAnywhere, Category = "MF")
	TArray<FVector> KeyPos;
	UPROPERTY(EditAnywhere, Category = "MF")
	TArray<FRotator> KeyRot;

	UPROPERTY(EditAnywhere, Category = "MF")
	float MoveTime = 1.f;
	UPROPERTY(EditAnywhere, Category = "MF")
	float StayOpenTime = 4.f;

	/** UE1 state name: BumpOpenTimed (default), StandOpenTimed, TriggerOpenTimed, None ... */
	UPROPERTY(EditAnywhere, Category = "MF")
	FName InitialState = TEXT("BumpOpenTimed");

	/** GameInfo.IsRelevant: only in this MF game class ("RageCTF" ...; none: any). */
	UPROPERTY(EditAnywhere, Category = "MF")
	FName CondGameType;
	UPROPERTY(EditAnywhere, Category = "MF")
	bool bTeamPlay = true;
	UPROPERTY(EditAnywhere, Category = "MF")
	bool bNonTeamPlay = true;

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
	void Open();
	void Close();
};
