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
	// --- Mobile Forces cheats (same names as the original; UE adds God, Ghost, Fly, Walk,
	//     Slomo, Summon, Teleport, PlayersOnly, ViewSelf, DestroyAll ...) ---------------
	/** Join team N and respawn there. */
	/** Developer: log what the view looks at (component, mesh, material, compile errors). */
	UFUNCTION(Exec)
	void MFTraceMat();

	UFUNCTION(Exec)
	void SetTeam(int32 N);
	/** Kill every enemy (everyone else in deathmatch). */
	UFUNCTION(Exec)
	void KillEnemy();
	/** Kill every bot. */
	UFUNCTION(Exec)
	void KillPawns();
	/** Kill all actors of a class (pawns die, other actors are destroyed), e.g. `KillAll MFCharacter`. */
	UFUNCTION(Exec)
	void KillAll(const FString& ClassName);
	/** Toggle damage immunity for your whole team. */
	UFUNCTION(Exec)
	void TeamGod();
	/** Full ammo for everything you carry. */
	UFUNCTION(Exec)
	void AllAmmo();
	/** Seconds left in the match. */
	UFUNCTION(Exec)
	void SetTime(int32 Seconds);
	/** Give every bot a weapon, e.g. `SetWeapons Shotgun`. */
	UFUNCTION(Exec)
	void SetWeapons(const FString& WeaponName);
	/** Give yourself a weapon, e.g. `GiveWeapon RocketLauncher`. */
	UFUNCTION(Exec)
	void GiveWeapon(const FString& WeaponName);
	/** Walk speed multiplier (1 = normal). */
	UFUNCTION(Exec)
	void SetSpeed(float Multiplier);
	/** Jump velocity in cm/s (default 731 = MF JumpZ 325). */
	UFUNCTION(Exec)
	void SetJumpZ(float Velocity);
	UFUNCTION(Exec)
	void ViewMe();

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
