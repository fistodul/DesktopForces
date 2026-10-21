#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CheatManager.h"
#include "MFCheatManager.generated.h"

/**
 * Debug console commands (UE cheat manager: available in standalone/PIE, or after
 * `EnableCheats`; never in shipping multiplayer).
 */
UCLASS()
class DESKTOPFORCES_API UMFCheatManager : public UCheatManager
{
	GENERATED_BODY()

public:
	// --- Mobile Forces cheats (same names as the original; UE adds God, Ghost, Fly, Walk,
	//     Slomo, Summon, Teleport, PlayersOnly, ViewSelf, DestroyAll ...) ---------------
	/** Join team N and respawn there. */
	/** Developer: log the navmesh path from your pawn to X Y Z (points, links). */
	UFUNCTION(Exec)
	void MFNavPath(float X, float Y, float Z);

	/** Developer: take N damage (scripted tests). */
	UFUNCTION(Exec)
	void MFHurt(int32 N);
	/** Developer: N damage from you to every enemy (mode rules apply: Tug converts, Totem saves). */
	UFUNCTION(Exec)
	void MFHurtEnemies(int32 N);
	/** Developer: press and release fire once (scripted tests); MFFire 1 = alt fire. */
	UFUNCTION(Exec)
	void MFFire(int32 Alt = 0);

	/**
	 * Developer: hold movement input for Seconds facing Yaw (Forward/Right -1..1), logging the
	 * position and movement mode every 0.25 s (scripted stairs / ladder tests).
	 */
	UFUNCTION(Exec)
	void MFWalk(float Yaw, float Seconds, float Forward = 1.f, float Right = 0.f);

	/** Developer: move your pawn to X Y Z (scripted tests). */
	UFUNCTION(Exec)
	void MFTeleport(float X, float Y, float Z);

	/** Turn to look at a world point (tests: then MFFire). */
	/** Give team order N (0 Defend .. 4 Freelance) to everyone, like the V menu. */
	UFUNCTION(Exec)
	void MFOrder(int32 Order);
	UFUNCTION(Exec)
	void MFAimAt(float X, float Y, float Z);

	/** Developer: log what the view looks at (component, mesh, material, compile errors). */
	UFUNCTION(Exec)
	void MFTraceMat();

	/** Get into seat N (0 driver, 1.. passengers) of the nearest vehicle in reach. */
	UFUNCTION(Exec)
	void MFEnterSeat(int32 Seat);

	/** Log every vehicle: position, physics, wheel compression. */
	UFUNCTION(Exec)
	void MFVehicles();

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
