#include "Weapons/MFWeapon_AdrenalineShot.h"
#include "Weapons/MFProjectile.h"
#include "Weapons/MFTripBomb.h"
#include "Player/MFCharacterMovement.h"
#include "Kismet/GameplayStatics.h"
#include "Player/MFCharacter.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "MFWeapons"

AMFWeapon_AdrenalineShot::AMFWeapon_AdrenalineShot()
{
	HUDIconRect = FIntRect(144, 0, 192, 64);   // WeaponIcon X=144 Y=0 W=48 H=64
	DisplayName = LOCTEXT("Adrenaline", "Adrenaline Shot");
	InventoryGroup = 10;
	CarrySize = 1;
	AutoSwitchPriority = 0.f;
	MaxClipAmmo = 1;
	MaxClips = 1;
	bDestroyWhenEmpty = true;
	// MF AdrenalineShot: no select / fire / down animations (empty Play*Anim): ready at once,
	// heals on the press and leaves the hands right after (Fire -> GotoState('DownWeapon'))
	FireTime = 0.f;
	EquipTime = 0.f;
	PutDownTime = 0.f;
	RecoilPower = 0.f;
}

bool AMFWeapon_AdrenalineShot::TryFire(bool bAlt)
{
	AMFCharacter* Me = GetOwnerCharacter();
	if (!Me || !HasAmmoInClip())
	{
		return false;
	}
	AMFCharacter* Target = Me;
	if (bAlt)
	{
		// MF TryToInject: trace Range along the view
		Target = nullptr;
		FHitResult Hit;
		const FVector Start = GetFireStart();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MFInject), false, Me);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + GetAimRotation().Vector() * InjectRange, ECC_GameTraceChannel2, Params))
		{
			Target = Cast<AMFCharacter>(Hit.GetActor());
		}
	}
	// MF InjectDrug: only used when the target is hurt
	if (!Target || Target->GetHealth() >= Target->MaxHealth)
	{
		return false;
	}
	SetState(EMFWeaponState::Firing, FireTime);
	ClipAmmo -= 1;
	if (HasAuthority())
	{
		Target->Heal(Target->MaxHealth);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
