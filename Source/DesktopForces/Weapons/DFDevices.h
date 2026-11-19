#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class ADFCharacter;
class ADFVehicle;
class AController;
class APlayerState;
struct FDamageEvent;

/**
 * Sides of the devices the MF-mods weapons leave behind (seeker mines, sentry turrets, trip
 * lasers): their IsFriendly / GetVehicleStatus / FindBestTarget, shared so a seeker mine goes
 * for an enemy sentry and a sentry shoots an enemy seeker mine, as in the mods. A device is
 * placed for a team in team games (its placer's at the time) and for its placer otherwise.
 */
namespace DF {
	/** MF-mods GetPlacedTeam: the team a seeker mine, sentry turret or trip laser was placed for
	 *  (DF::NoTeam in deathmatch). False for anything else. */
	DESKTOPFORCES_API bool GetPlacedTeam(const AActor* A, uint8& OutTeam);
	/** A device still working: a living soldier, a turret not blown up, a mine not gone off. */
	DESKTOPFORCES_API bool IsDeviceTargetAlive(const AActor* A);
	/** IsPawnFriendly: the dead and nobody count as friends; then the team in team games, else
	 *  only the placer (by player: still them after they respawn). */
	DESKTOPFORCES_API bool IsSoldierFriendly(const ADFCharacter* C, uint8 Team, const APlayerState* Placer);
	/** GetVehicleStatus: 0 nobody inside, 1 only friends, 2 an enemy inside. */
	DESKTOPFORCES_API uint8 VehicleStatus(const ADFVehicle* V, uint8 Team, const APlayerState* Placer);
	/** IsFriendly of a device (Device, placed for Team by Placer): other devices by their team
	 *  (deathmatch: by their placer), vehicles by who is in them (empty: friendly), soldiers as
	 *  IsSoldierFriendly. Anything else - walls, props - is never a target. */
	DESKTOPFORCES_API bool IsDeviceFriendly(const AActor* Other, const AActor* Device, uint8 Team, const APlayerState* Placer);
	/** FindBestTarget: the nearest within Range of vehicles with an enemy inside, enemy
	 *  soldiers, sentry turrets and seeker mines; Usable (line of sight, for the turret) last. */
	DESKTOPFORCES_API AActor* FindDeviceTarget(const AActor* Device, uint8 Team, const APlayerState* Placer, float Range,
		TFunctionRef<bool(const AActor*)> Usable);
	/** SentryGunTurret.CheckVehicleRunOver: a vehicle at KillSpeed runs a device down, unless
	 *  a friend drives it (an empty one still does). For a device's TakeDamage. */
	DESKTOPFORCES_API bool IsRunOverByFriend(const FDamageEvent& Event, const AController* Driver, const AActor* Device, uint8 Team, const APlayerState* Placer);
} // namespace DF
