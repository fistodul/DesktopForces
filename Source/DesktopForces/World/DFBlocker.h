#pragma once

#include "CoreMinimal.h"
#include "Core/DFTypes.h"
#include "GameFramework/Actor.h"
#include "DFBlocker.generated.h"

class UStaticMeshComponent;

/**
 * An invisible UE1 collision cylinder that blocks the way:
 * - mappers' Triggers with bBlockPlayers / bBlockActors (invisible walls, map edges): soldiers,
 *   and vehicles too with bBlockActors;
 * - RageGame.LoadoutBlocker (Team set; team games only): a team's spawn room door. Its own
 *   team walks through; enemies, captains and flag / key carriers don't, nor any projectile,
 *   nor players' shots (IsBlockedBy / IsTraceBlockedBy).
 * Placed by `mfport.py import --steps patch`.
 */
UCLASS()
class DESKTOPFORCES_API ADFBlocker : public AActor {
	GENERATED_BODY()

public:
	ADFBlocker();

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<UStaticMeshComponent> Shape;

	/** LoadoutBlocker.Team (DF::NoTeam: a plain blocker for everyone). */
	UPROPERTY(EditAnywhere, Category = "DF")
	uint8 Team = DF::NoTeam;

	UPROPERTY(EditAnywhere, Category = "DF")
	bool bBlockVehicles = false;

	/** Size of the UE1 cylinder (cm). */
	void SetCylinder(float Radius, float HalfHeight);

protected:
	virtual void BeginPlay() override;
	/** LoadoutBlocker: who may pass, looked at 4 times a second for soldiers nearby. */
	void UpdatePasses();
	FTimerHandle PassTimer;
	float Reach = 0.f;
};
