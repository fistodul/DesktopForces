#include "Weapons/AdrenalineShot/AdrenalineShot.h"
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

AAdrenalineShot::AAdrenalineShot()
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

bool AAdrenalineShot::TryFire(bool bAlt)
{
	ADFCharacter* Me = GetOwnerCharacter();
	if (!Me || !HasAmmoInClip())
	{
		return false;
	}
	ADFCharacter* Target = Me;
	if (bAlt)
	{
		// MF TryToInject: trace Range along the view
		Target = nullptr;
		FHitResult Hit;
		const FVector Start = GetFireStart();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DFInject), false, Me);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + GetAimRotation().Vector() * InjectRange, ECC_GameTraceChannel2, Params))
		{
			Target = Cast<ADFCharacter>(Hit.GetActor());
		}
	}
	// MF InjectDrug: only used when the target is hurt
	if (!Target || Target->GetHealth() >= Target->MaxHealth)
	{
		return false;
	}
	SetState(EDFWeaponState::Firing, FireTime);
	ClipAmmo -= 1;
	if (HasAuthority())
	{
		Target->Heal(Target->MaxHealth);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
