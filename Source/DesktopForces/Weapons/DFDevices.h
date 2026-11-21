#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UObject/Interface.h"
#include "DFDevices.generated.h"

class ADFCharacter;
class AController;
class APlayerState;
struct FDamageEvent;

UINTERFACE(MinimalAPI)
class UDFDevice : public UInterface {
	GENERATED_BODY()
};

/**
 * Something a player leaves behind for their side: a seeker mine, a sentry turret, a planted
 * trip bomb or laser, a thrown mine (MF-mods' "PlacedTeam" actors). Placed for a team in team
 * games (the placer's at the time; switching teams doesn't take it along) and for the placer
 * otherwise, by player, so still theirs after they respawn.
 */
class DESKTOPFORCES_API IDFDevice {
	GENERATED_BODY()
public:
	virtual uint8 GetPlacedTeam() const = 0;
	/** Whose it is (null once they've left). */
	virtual const APlayerState* GetPlacer() const = 0;
	/** Still a working device: not blown up, gone off or spent. */
	virtual bool IsWorking() const { return true; }
};

/**
 * The sides of devices (MF-mods SeekerMine / SentryGunTurret / TripLaser IsFriendly,
 * GetVehicleStatus, FindBestTarget), shared so a seeker mine goes for an enemy sentry and a
 * sentry shoots an enemy seeker mine, as in the mods.
 */
namespace DF {
	/** A device's view of anything: on its side, against it, or neither (walls, props, an
	 *  empty vehicle). */
	enum class ESide : uint8 {
		Friend,
		Enemy,
		Neutral,
	};
	/** What a device (Device, placed for Team by Placer) makes of Other: other devices by their
	 *  team (deathmatch: by their placer), vehicles by who is in them (an enemy inside: Enemy,
	 *  only friends: Friend, nobody: Neutral), soldiers by team or placer, the dead as friends
	 *  (MF IsPawnFriendly). */
	DESKTOPFORCES_API ESide SideOf(const AActor* Other, const AActor* Device, uint8 Team, const APlayerState* Placer);
	/** IsPawnFriendly: the dead and nobody count as friends; then the team in team games, else
	 *  only the placer. */
	DESKTOPFORCES_API bool IsSoldierFriendly(const ADFCharacter* C, uint8 Team, const APlayerState* Placer);
	/** A unit, not level geometry: a soldier, a vehicle or a device (what a thrown knife hurts and
	 *  a trip laser's beam goes off on). */
	DESKTOPFORCES_API bool IsPawnOrDevice(const AActor* A);
	/** Worth hunting or shooting: a living soldier, a vehicle not blown up, a working device. */
	DESKTOPFORCES_API bool IsDeviceTargetAlive(const AActor* A);
	/** FindBestTarget: the nearest Enemy within Range of vehicles, soldiers, sentry turrets and
	 *  seeker mines that is Usable (line of sight, for the turret; asked nearest first). */
	DESKTOPFORCES_API AActor* FindDeviceTarget(const AActor* Device, uint8 Team, const APlayerState* Placer, float Range,
		TFunctionRef<bool(const AActor*)> Usable);
	/** SentryGunTurret.CheckVehicleRunOver: a vehicle at KillSpeed runs a device down, unless
	 *  a friend drives it (an empty one still does). For a device's TakeDamage. */
	DESKTOPFORCES_API bool IsRunOverByFriend(const FDamageEvent& Event, const AController* Driver, const AActor* Device, uint8 Team, const APlayerState* Placer);
} // namespace DF
