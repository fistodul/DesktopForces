#include "Weapons/Armour/MFWeapon_Armour.h"
#include "Weapons/MFProjectile.h"
#include "Weapons/TripBombs/MFTripBomb.h"
#include "Player/MFCharacterMovement.h"
#include "Kismet/GameplayStatics.h"
#include "Player/MFCharacter.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "MFWeapons"

AMFWeapon_Armour::AMFWeapon_Armour()
{
	HUDIconRect = FIntRect(0, 64, 64, 128);   // WeaponIcon X=0 Y=64 W=64 H=64
	DisplayName = LOCTEXT("Armour", "Body Armour");
	InventoryGroup = 11;
	CarrySize = 1;
	bSelectable = false;
	AutoSwitchPriority = 0.f;
	AIRating = -1.f;
	MaxClipAmmo = 1;
	MaxClips = 1;
	bHideAmmoCount = true;
}

void AMFWeapon_Armour::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFWeapon_Armour, Charge);
}

void AMFWeapon_Armour::GiveFullAmmo()
{
	Super::GiveFullAmmo();
	Charge = MaxCharge;
}

int32 AMFWeapon_Armour::AbsorbDamage(int32 Damage, const UMFDamageType* Type)
{
	// Inventory.ArmorAbsorbDamage + RageArmour: drowning and falling bypass armour
	if (Type && Type->bBypassArmour)
	{
		return Damage;
	}
	int32 ArmorDamage = Damage * ArmorAbsorption / 100;
	if (ArmorDamage >= Charge)
	{
		ArmorDamage = Charge;
		Charge = 0;
		if (AMFCharacter* C = GetOwnerCharacter())
		{
			C->RemoveWeapon(this, true);   // worn out
		}
	}
	else
	{
		Charge -= ArmorDamage;
	}
	return Damage - ArmorDamage;
}

#undef LOCTEXT_NAMESPACE
