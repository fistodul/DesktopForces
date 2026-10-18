#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "MFTypes.generated.h"

/** Team numbers as used by Mobile Forces maps (PlayerStart.TeamNumber, RageFlagBase.Team ...). */
UENUM(BlueprintType)
enum class EMFTeam : uint8
{
	Blue = 0,
	Red = 1,
	None = 255 UMETA(DisplayName = "None / Neutral"),
};

namespace MF
{
	inline int32 TeamIndex(EMFTeam Team) { return Team == EMFTeam::None ? INDEX_NONE : static_cast<int32>(Team); }
	inline EMFTeam OtherTeam(EMFTeam Team) { return Team == EMFTeam::Blue ? EMFTeam::Red : (Team == EMFTeam::Red ? EMFTeam::Blue : EMFTeam::None); }
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
