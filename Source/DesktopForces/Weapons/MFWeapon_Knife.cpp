#include "Weapons/MFWeapon_Knife.h"
#include "DesktopForces.h"
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

AMFWeapon_Knife::AMFWeapon_Knife()
{
	ViewMesh = TEXT("RageWeapons/KnifePickupCarryMesh");   // PickupViewMesh / ThirdPersonMesh
	ViewRotation = FRotator(-70.f, 0.f, 0.f);              // the blade stands up in the model: point it forward
	ViewOffset = FVector(16.f, 10.f, 9.f);
	HUDIconRect = FIntRect(96, 0, 144, 64);   // WeaponIcon X=96 Y=0 W=48 H=64
	DisplayName = LOCTEXT("Knife", "Knife");
	InventoryGroup = 1;
	CarrySize = 0;
	bLoadoutSelectable = false;
	bUndroppable = true;
	MaxClipAmmo = 1;
	MaxClips = 1;
	FireTime = 0.55f;          // Slash01-04 at rate 1.5 (0.49-0.61 s)
	ReloadTime = 0.83f;        // ThrowUpCatch at 0.8
	EquipTime = 0.33f;         // SelectUp
	PutDownTime = 0.33f;
	HitDamage = 30.f;
	HitMomentum = 33750.f;     // MF 15000
	DamageType = UMFDamage_Knife::StaticClass();
	AltProjectileClass = AMFProjectile_Knife::StaticClass();
	bInstantHit = true;
	RecoilPower = 0.f;
	MFFireSound = TEXT("WeaponSFX_Knife.Fire01");
	MFSelectSound = TEXT("WeaponSFX_Knife.Select");
}

void AMFWeapon_Knife::FireShot(bool bAlt)
{
	if (!bAlt)
	{
		// Melee: no ammo used
		bLastShotAlt = false;
		if (HasAuthority())
		{
			++BurstCounter;
			const FVector Start = GetFireStart();
			const FVector Dir = GetAimRotation().Vector();
			FCollisionQueryParams Params(SCENE_QUERY_STAT(MFKnife), false, GetOwner());
			Params.AddIgnoredActor(GetOwner() ? GetOwner()->GetAttachParentActor() : nullptr);
			FHitResult Hit;
			if (GetWorld()->SweepSingleByChannel(Hit, Start, Start + Dir * MeleeRange, FQuat::Identity, ECC_GameTraceChannel2,
				FCollisionShape::MakeSphere(10.f), Params))
			{
				ProcessTraceHit(Hit, Dir, false);
			}
		}
		if (IsLocallyControlled())
		{
			PlayFireEffects(false);
		}
		return;
	}
	// Throw: the knife leaves the hand until it "returns"
	ClipAmmo = 0;
	if (HasAuthority())
	{
		++BurstCounter;
		if (AMFProjectile_Knife* K = Cast<AMFProjectile_Knife>(SpawnProjectile(AltProjectileClass, 0.f, true)))
		{
			K->OwnerKnife = this;
			K->ReturnTime = KnifeReturnTime;
		}
		// safety net only: the knife itself says when it's back
		GetWorldTimerManager().SetTimer(ReturnTimer, this, &AMFWeapon_Knife::KnifeReturned, KnifeReturnTime + 30.f, false);
	}
}

void AMFWeapon_Knife::Finish()
{
	// No reloading: an empty knife just idles until it comes back
	State = EMFWeaponState::Idle;
	if (bFireHeld && HasAmmoInClip())
	{
		TryFire(false);
	}
}

void AMFWeapon_Knife::KnifeReturned()
{
	GetWorldTimerManager().ClearTimer(ReturnTimer);
	ClipAmmo = 1;
	UE_LOG(LogMF, Verbose, TEXT("%s: the thrown knife is back"), *GetName());
}

#undef LOCTEXT_NAMESPACE
