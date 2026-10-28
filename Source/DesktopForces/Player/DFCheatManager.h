#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CheatManager.h"
#include "DFCheatManager.generated.h"

/**
 * Game/debug console commands in every build, restricted by CanCheat and ServerAdmin.
 */
UCLASS()
class DESKTOPFORCES_API UDFCheatManager : public UCheatManager {
	GENERATED_BODY()

public:
	/** On a client: server cheats go to the server (ADFPlayerController::ServerAdmin, which
	 *  checks the player may cheat); views and test helpers run here. */
	virtual bool ProcessConsoleExec(const TCHAR* Cmd, FOutputDevice& Ar, UObject* Executor) override;

	// --- admin (ZombiePlayer's, for every mode; admins, or anyone with cheats on) ------------
	/** Run a console command as another player: RunAs <player> <command>. */
	UFUNCTION(Exec)
	void RunAs(const FString& PlayerAndCommand);
	/** ... as the player you look at. */
	UFUNCTION(Exec)
	void RunAsLooking(const FString& Command);
	/** Set / read a property of a player's soldier (then their player state, controller):
	 *  SetTo <player> <property> <value>, GetTo <player> <property>. */
	UFUNCTION(Exec)
	void SetTo(const FString& PlayerPropertyValue);
	UFUNCTION(Exec)
	void GetTo(const FString& PlayerAndProperty);
	UFUNCTION(Exec)
	void SetToSelf(const FString& PropertyAndValue);
	UFUNCTION(Exec)
	void GetToSelf(const FString& Property);
	/** ... of whatever you look at. */
	UFUNCTION(Exec)
	void SetToLooking(const FString& PropertyAndValue);
	UFUNCTION(Exec)
	void GetToLooking(const FString& Property);
	/** Go next to a player (ZombiePlayer.Teleport; UE's Teleport goes where you look). */
	UFUNCTION(Exec)
	void TeleportTo(const FString& PlayerName);
	/** Zombie mode: a human's / survivor's / zombie's / nemesis' stats (not a team change). */
	UFUNCTION(Exec)
	void BecomeHuman();
	UFUNCTION(Exec)
	void BecomeSurvivor();
	UFUNCTION(Exec)
	void BecomeZombie();
	UFUNCTION(Exec)
	void BecomeNemesis();
	/** Admins: throw a player out; change the map (SwitchMap mf-warehouse?game=ctf). */
	UFUNCTION(Exec)
	void Kick(const FString& PlayerName);
	UFUNCTION(Exec)
	void SwitchMap(const FString& URL);

	// --- Mobile Forces cheats (same names as the original; UE adds God, Ghost, Fly, Walk,
	//     Slomo, Summon, Teleport, PlayersOnly, ViewSelf, DestroyAll ...) ---------------
	/** Join team N and respawn there. */
	/** Developer: log the navmesh path from your pawn to X Y Z (points, links). */
	UFUNCTION(Exec)
	void DFNavPath(float X, float Y, float Z);

	/** Developer: take N damage (scripted tests). */
	UFUNCTION(Exec)
	void DFHurt(int32 N);
	/** Developer: N damage from you to every enemy (mode rules apply: Tug converts, Totem saves). */
	UFUNCTION(Exec)
	void DFHurtEnemies(int32 N);
	/** Developer: hold fire for Seconds (scripted tests); DFFire 1 = alt fire. */
	UFUNCTION(Exec)
	void DFFire(int32 Alt = 0, float Seconds = 0.1f);

	/**
	 * Developer: hold movement input for Seconds facing Yaw (Forward/Right -1..1), logging the
	 * position and movement mode every 0.25 s (scripted stairs / ladder tests).
	 */
	UFUNCTION(Exec)
	void DFWalk(float Yaw, float Seconds, float Forward = 1.f, float Right = 0.f, float Up = 0.f);

	/** Developer: move your pawn to X Y Z (scripted tests). */
	UFUNCTION(Exec)
	void DFTeleport(float X, float Y, float Z);

	/** Turn to look at a world point (tests: then DFFire). */
	/** Give team order N (0 Defend .. 4 Freelance) to everyone, like the V menu. */
	UFUNCTION(Exec)
	void DFOrder(int32 Order);
	UFUNCTION(Exec)
	void DFAimAt(float X, float Y, float Z);
	/** Turn to the nearest actor whose class is called ClassName (or derives from it). */
	UFUNCTION(Exec)
	void DFAimAtNearest(const FString& ClassName);
	/** Move yourself (or what you drive) Dist cm in front of the nearest actor of a class (tests). */
	UFUNCTION(Exec)
	void DFTeleportNear(const FString& ClassName, float Dist = 600.f);

	/** Developer: log what the view looks at (component, mesh, material, compile errors). */
	UFUNCTION(Exec)
	void DFTraceMat();

	/** Get into seat N (0 driver, 1.. passengers) of the nearest vehicle in reach. */
	UFUNCTION(Exec)
	void DFEnterSeat(int32 Seat);

	/** Log every vehicle: position, physics, wheel compression. */
	/** Blow up the nearest vehicle (explosive damage, as a rocket would). */
	UFUNCTION(Exec)
	void DFBlowUp();
	UFUNCTION(Exec)
	void DFVehicles();
	/** Put a mark where you look: 0 bullet hole, 1 scorch (decal tests). */
	UFUNCTION(Exec)
	void DFDecal(int32 Kind, float Size = 0.f);
	/** Log movable components that can change the navmesh ("NAVMESH NEEDS TO BE REBUILT" hunt). */
	UFUNCTION(Exec)
	void DFNavRelevant();

	UFUNCTION(Exec)
	void SetTeam(int32 N);
	/** Kill every enemy (everyone else in deathmatch). */
	UFUNCTION(Exec)
	void KillEnemy();
	/** Kill every bot. */
	UFUNCTION(Exec)
	void KillPawns();
	/** Kill all actors of a class (pawns die, other actors are destroyed), e.g. `KillAll DFCharacter`. */
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
	void DFNavTest();

	/**
	 * Orthographic top-down view centred at X,Y covering Width cm (0 = back to the pawn),
	 * camera at height Z (0 = high above the map). A camera under the roofs cuts them away,
	 * for textured floor plans (Tools/mfport/debug/merge_shots.py).
	 */
	UFUNCTION(Exec)
	void DFTopView(float X, float Y, float Width, float Z);

	/** Perspective camera at X,Y,Z looking towards yaw/pitch (degrees); for scripted shots. */
	UFUNCTION(Exec)
	void DFCam(float X, float Y, float Z, float Yaw, float Pitch);

	/** Camera riding along with your own pawn (Other 1: the nearest other one), Dist cm out at
	 *  Yaw from where it faces (0 = in front of it, 180 = behind), looking back at it: model and
	 *  animation checks. */
	UFUNCTION(Exec)
	void DFOrbit(float Yaw = 180.f, float Dist = 250.f, int32 Other = 0);

private:
	/** The player (pawn, else controller) named so, any case. */
	AActor* FindPlayer(const FString& Name, AController** OutController = nullptr) const;
	/** What you look at (10000 uu like MF). */
	AActor* LookedAt() const;
	void Say(const FString& Line) const;
	bool IsAdmin() const;
};
