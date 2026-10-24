#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "UObject/Interface.h"
#include "DFTypes.generated.h"

namespace DF {
	/**
	 * Teams are plain ids: an index into UDFGameSettings::Teams (0 blue, 1 red, ... as many
	 * as configured). 255 means "no team" - the same value as MF's neutral PlayerStart
	 * TeamNumber and UE's FGenericTeamId::NoTeam, so AI perception understands it too.
	 */
	inline constexpr uint8 NoTeam = 255;
} // namespace DF

/**
 * Base damage type. MF identified damage by name ('tracedshot', 'Fell' ...);
 * each name is a subclass here. Flags reproduce the special cases in MF script.
 */
UCLASS(Abstract)
class DESKTOPFORCES_API UDFDamageType : public UDamageType {
	GENERATED_BODY()
public:
	/** Explosions (rockets, grenades, mines, burning wrecks). */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	bool bExplosive = false;

	/**
	 * Damage to a vehicle's hull, times (1 - its Armour). MF: explosives x 1.5, nothing else;
	 * the community servers' mutPack changeDamage let other hits through at 10 / 80, as
	 * Desktop Forces does. Knives, fire
	 * and the like carry 0 or a small share; mods give their damage types their own value.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "DF|Vehicles")
	float VehicleDamageScale = 0.f;

	/** Gunpowder (bullets, pellets): bulletproof vehicles (Armadillo APCs) shrug it off. */
	UPROPERTY(EditDefaultsOnly, Category = "DF|Vehicles")
	bool bBallistic = false;

	/** Can hit a vehicle's petrol cap (150 of it and the vehicle blows up): MF's bullets and
	 *  pellets; a knife too (its one way to hurt a vehicle). */
	UPROPERTY(EditDefaultsOnly, Category = "DF|Vehicles")
	bool bHitsPetrolCap = false;

	/** Armour does not absorb this ('Drowned', 'Fell' in RageArmour.ArmorAbsorbDamage). */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	bool bBypassArmour = false;

	/** Text for the kill message: "<Killer> <Verb> <Victim>". */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	FText DeathVerb = NSLOCTEXT("MF", "Killed", "killed");
};

/** 'tracedshot' - pistol, machine gun, rifle, HMG bullets. */
UCLASS()
class DESKTOPFORCES_API UDFDamage_Bullet : public UDFDamageType {
	GENERATED_BODY()
public:
	UDFDamage_Bullet() {
		VehicleDamageScale = 0.125f;
		bBallistic = true;
		bHitsPetrolCap = true;
	}
};
/** 'scattershot' - shotgun pellets. */
UCLASS()
class DESKTOPFORCES_API UDFDamage_Shotgun : public UDFDamageType {
	GENERATED_BODY()
public:
	UDFDamage_Shotgun() {
		VehicleDamageScale = 0.125f;
		bBallistic = true;
		bHitsPetrolCap = true;
	}
};
/** 'ArmourPiercing' - knife slash and throw: no harm to a vehicle's hull (changeDamage
 *  knivesDamageVehicles off), only to its petrol cap. */
UCLASS()
class DESKTOPFORCES_API UDFDamage_Knife : public UDFDamageType {
	GENERATED_BODY()
public:
	UDFDamage_Knife() { bHitsPetrolCap = true; }
};
/** 'Decapitated' - headshots and thrown knife to the head. */
UCLASS()
class DESKTOPFORCES_API UDFDamage_Headshot : public UDFDamageType {
	GENERATED_BODY()
};
/** Rockets, grenades, trip bombs, exploding vehicles. */
UCLASS()
class DESKTOPFORCES_API UDFDamage_Explosion : public UDFDamageType {
	GENERATED_BODY()
public:
	UDFDamage_Explosion() {
		bExplosive = true;
		VehicleDamageScale = 1.5f;
	}
};
/** 'Fell' */
UCLASS()
class DESKTOPFORCES_API UDFDamage_Fell : public UDFDamageType {
	GENERATED_BODY()
public:
	UDFDamage_Fell() { bBypassArmour = true; }
};
/** 'Drowned' */
UCLASS()
class DESKTOPFORCES_API UDFDamage_Drowned : public UDFDamageType {
	GENERATED_BODY()
public:
	UDFDamage_Drowned() { bBypassArmour = true; }
};
/** 'Crushed' - movers, vehicles running players over. */
UCLASS()
class DESKTOPFORCES_API UDFDamage_Crushed : public UDFDamageType {
	GENERATED_BODY()
};
/** 'Suicided' - console suicide, team change. */
UCLASS()
class DESKTOPFORCES_API UDFDamage_Suicide : public UDFDamageType {
	GENERATED_BODY()
public:
	UDFDamage_Suicide() { bBypassArmour = true; }
};

/** Team orders, in MF orders menu order (Rage.int OrderAbbrev 0-4; RageBot orders). */
UENUM(BlueprintType)
enum class EDFBotOrder : uint8 {
	Defend, // guard the team's objective / base
	Hold, // "Defend this area": stay where the order was given
	Attack, // go for the enemy objective / base
	Follow, // "Cover me": stay with whoever gave the order
	Freelance, // "Improvise": do what bots do
};

UINTERFACE(MinimalAPI)
class UDFDamageFilter : public UInterface {
	GENERATED_BODY()
};

/**
 * Something on a character (usually a component a mod adds: a buff, a shield) that sees
 * damage before armour does. Return the damage left (0 = none taken).
 */
class DESKTOPFORCES_API IDFDamageFilter {
	GENERATED_BODY()
public:
	virtual int32 FilterDamage(int32 Damage, const UDFDamageType* Type) = 0;
};
