#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "MFTypes.generated.h"

namespace MF
{
	/**
	 * Teams are plain ids: an index into UMFGameSettings::Teams (0 blue, 1 red, ... as many
	 * as configured). 255 means "no team" - the same value as MF's neutral PlayerStart
	 * TeamNumber and UE's FGenericTeamId::NoTeam, so AI perception understands it too.
	 */
	inline constexpr uint8 NoTeam = 255;
}

/**
 * Base damage type. MF identified damage by name ('tracedshot', 'Fell' ...);
 * each name is a subclass here. Flags reproduce the special cases in MF script.
 */
UCLASS(Abstract)
class MOBILEFORCES_API UMFDamageType : public UDamageType
{
	GENERATED_BODY()
public:
	/** Explosive damage hurts vehicles; MF gives 0 damage to vehicles from everything else. */
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	bool bExplosive = false;

	/** Armour does not absorb this ('Drowned', 'Fell' in RageArmour.ArmorAbsorbDamage). */
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	bool bBypassArmour = false;

	/** Text for the kill message: "<Killer> <Verb> <Victim>". */
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	FText DeathVerb = NSLOCTEXT("MF", "Killed", "killed");
};

/** 'tracedshot' - pistol, machine gun, rifle, HMG bullets. */
UCLASS()
class MOBILEFORCES_API UMFDamage_Bullet : public UMFDamageType
{
	GENERATED_BODY()
};
/** 'scattershot' - shotgun pellets. */
UCLASS()
class MOBILEFORCES_API UMFDamage_Shotgun : public UMFDamageType
{
	GENERATED_BODY()
};
/** 'ArmourPiercing' - knife slash. */
UCLASS()
class MOBILEFORCES_API UMFDamage_Knife : public UMFDamageType
{
	GENERATED_BODY()
};
/** 'Decapitated' - headshots and thrown knife to the head. */
UCLASS()
class MOBILEFORCES_API UMFDamage_Headshot : public UMFDamageType
{
	GENERATED_BODY()
};
/** Rockets, grenades, trip bombs, exploding vehicles. */
UCLASS()
class MOBILEFORCES_API UMFDamage_Explosion : public UMFDamageType
{
	GENERATED_BODY()
public:
	UMFDamage_Explosion() { bExplosive = true; }
};
/** 'Fell' */
UCLASS()
class MOBILEFORCES_API UMFDamage_Fell : public UMFDamageType
{
	GENERATED_BODY()
public:
	UMFDamage_Fell() { bBypassArmour = true; }
};
/** 'Drowned' */
UCLASS()
class MOBILEFORCES_API UMFDamage_Drowned : public UMFDamageType
{
	GENERATED_BODY()
public:
	UMFDamage_Drowned() { bBypassArmour = true; }
};
/** 'Crushed' - movers, vehicles running players over. */
UCLASS()
class MOBILEFORCES_API UMFDamage_Crushed : public UMFDamageType
{
	GENERATED_BODY()
};
/** 'Suicided' - console suicide, team change. */
UCLASS()
class MOBILEFORCES_API UMFDamage_Suicide : public UMFDamageType
{
	GENERATED_BODY()
public:
	UMFDamage_Suicide() { bBypassArmour = true; }
};

/** Team orders, in MF orders menu order (Rage.int OrderAbbrev 0-4; RageBot orders). */
UENUM(BlueprintType)
enum class EMFBotOrder : uint8
{
	Defend,     // guard the team's objective / base
	Hold,       // "Defend this area": stay where the order was given
	Attack,     // go for the enemy objective / base
	Follow,     // "Cover me": stay with whoever gave the order
	Freelance,  // "Improvise": do what bots do
};
