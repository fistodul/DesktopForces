#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CheatManager.h"
#include "MFCheatManager.generated.h"

/**
 * Debug console commands (UE cheat manager: available in standalone/PIE, or after
 * `EnableCheats`; never in shipping multiplayer).
 */
UCLASS()
class MOBILEFORCES_API UMFCheatManager : public UCheatManager
{
	GENERATED_BODY()

public:
	/** Log navmesh paths from the pawn to every player start, objective and ladder link. */
	UFUNCTION(Exec)
	void MFNavTest();

	/**
	 * Orthographic top-down view centred at X,Y covering Width cm (0 = back to the pawn),
	 * camera at height Z (0 = high above the map). A camera under the roofs cuts them away,
	 * for textured floor plans (Tools/mfport/debug/merge_shots.py).
	 */
	UFUNCTION(Exec)
	void MFTopView(float X, float Y, float Width, float Z);

	/** Perspective camera at X,Y,Z looking towards yaw/pitch (degrees); for scripted shots. */
	UFUNCTION(Exec)
	void MFCam(float X, float Y, float Z, float Yaw, float Pitch);
};
