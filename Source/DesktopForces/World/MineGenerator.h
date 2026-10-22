#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MineGenerator.generated.h"

/**
 * A map's minefield (mineGenerator.u, mf-Pobojowisko_VerII): a proximity mine on the ground,
 * nobody's, set off by soldiers or vehicles coming close (vehicles from further); a new one 5 s
 * after it went (mineGenerator.checkInterval). bSplash (splashMakerGen): no mine, just a splash
 * of the river when something wades through. Placed from the map's legacy actors at the start.
 */
UCLASS(NotPlaceable)
class DESKTOPFORCES_API AMineGenerator : public AActor
{
	GENERATED_BODY()

public:
	AMineGenerator();
	/** Server: one for each mineGenerator / splashMakerGen the map has. */
	static void SpawnAll(UWorld* World);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** A splashMakerGen: no mine, a river splash (set before spawning; clients get it with the
	 *  actor, before their BeginPlay). */
	UPROPERTY(Replicated)
	bool bSplash = false;

protected:
	virtual void BeginPlay() override;
	void Check();
	void Arm();
	void SetOff();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastBlast(bool bWasSplash);
	UFUNCTION()
	void OnRep_Armed();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UStaticMeshComponent> Mesh;
	UPROPERTY(ReplicatedUsing = OnRep_Armed)
	bool bArmed = false;
	FTimerHandle CheckTimer;
	FTimerHandle RearmTimer;
};
