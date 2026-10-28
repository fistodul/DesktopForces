#include "Weapons/Armour/Armour.h"
#include "Weapons/DFProjectile.h"
#include "Weapons/TripBombs/TripBomb.h"
#include "Player/DFCharacterMovement.h"
#include "Kismet/GameplayStatics.h"
#include "Player/DFCharacter.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "DFWeapons"

AArmour::AArmour() {
	HUDIconRect = FIntRect(0, 64, 64, 128); // WeaponIcon X=0 Y=64 W=64 H=64
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

void AArmour::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AArmour, Charge);
}

void AArmour::SetReinforced(bool bOn) {
	const int32 NewMax = bOn ? 120 : 100; // ZombieArmour Charge 120, ArmorAbsorption 80
	if (NewMax == MaxCharge)
		return;
	MaxCharge = NewMax;
	ArmorAbsorption = bOn ? 80 : 75;
	DisplayName = bOn ? LOCTEXT("ZombieArmour", "Zombie Armour") : GetDefault<AArmour>(GetClass())->DisplayName;
	GiveFullAmmo();
}

void AArmour::GiveFullAmmo() {
	Super::GiveFullAmmo();
	Charge = MaxCharge;
}

int32 AArmour::AbsorbDamage(int32 Damage, const UDFDamageType* Type) {
	// Inventory.ArmorAbsorbDamage + RageArmour: drowning and falling bypass armour
	if (Type && Type->bBypassArmour)
		return Damage;
	int32 ArmorDamage = Damage * ArmorAbsorption / 100;
	if (ArmorDamage >= Charge) {
		ArmorDamage = Charge;
		Charge = 0;
		if (ADFCharacter* C = GetOwnerCharacter())
			C->RemoveWeapon(this, true); // worn out
	} else {
		Charge -= ArmorDamage;
	}
	return Damage - ArmorDamage;
}

#undef LOCTEXT_NAMESPACE
