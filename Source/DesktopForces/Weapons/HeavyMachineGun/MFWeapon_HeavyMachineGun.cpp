#include "Weapons/HeavyMachineGun/MFWeapon_HeavyMachineGun.h"
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

AMFWeapon_HeavyMachineGun::AMFWeapon_HeavyMachineGun()
{
	ViewMesh = TEXT("RageWeapons/HeavyMachineGunPickupCarryMesh");
	HUDIconRect = FIntRect(64, 64, 256, 128);   // WeaponIcon X=64 Y=64 W=192 H=64
	DisplayName = LOCTEXT("HMG", "Heavy Machine Gun");
	InventoryGroup = 6;
	CarrySize = 4;
	AIRating = 0.9f;
	MaxClipAmmo = 900;
	MaxClips = 1;
	EquipTime = 0.667f;         // SelectUp 21 @30
	PutDownTime = 0.667f;
	ZoomTime = 2.526f;          // deploy: ActivateGun01 41 frames @30 x1.3, 02 31 @30, 03 31 @30 x2
	HitDamage = 45.f;
	Accuracy = 0.6f;            // spread at standstill ...
	AltAccuracy = 1.2f;         // ... and at full spin
	MaxTraceDistance = 11250.f; // MF 5000
	MaxZoomLevel = 1;           // MF models "deployed" as zoom level 1
	ZoomScale = 1.f;
	ZoomedMoveScale = 0.f;      // can't move while deployed
	RecoilPower = 0.3f;
	MFFireSound = TEXT("WeaponSFX_HeavyMachineGun.Fire");
	MFSelectSound = TEXT("WeaponSFX_HeavyMachineGun.Select");
}

void AMFWeapon_HeavyMachineGun::ToggleZoom()
{
	// MF OKToDeploy: only standing on the ground (not in water, not falling)
	const AMFCharacter* C = GetOwnerCharacter();
	if (ZoomLevel == 0 && (!C || !C->GetCharacterMovement()->IsMovingOnGround()))
	{
		return;
	}
	const bool bDeploying = ZoomLevel == 0;
	if (ZoomLevel > 0)
	{
		GetWorldTimerManager().ClearTimer(SpinTimer);
		SpinRate = 0.f;
	}
	GetWorldTimerManager().ClearTimer(DeploySoundTimer);
	Super::ToggleZoom();
	if (bDeploying && State == EMFWeaponState::Zooming)
	{
		// PlayAimUpAnim / PlayReloadingAnim / PlayPullbackAnim: tripod, magazine, step back
		PlayMFSound(TEXT("WeaponSFX_HeavyMachineGun.SetupTripod"));
		GetWorldTimerManager().SetTimer(DeploySoundTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			PlayMFSound(TEXT("WeaponSFX_HeavyMachineGun.SetupMagazine"));
			GetWorldTimerManager().SetTimer(DeploySoundTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				PlayMFSound(TEXT("WeaponSFX_HeavyMachineGun.SetupStepBack"));
			}), 1.f, false);
		}), 1.026f, false);
	}
	else if (!bDeploying)
	{
		SetState(EMFWeaponState::Zooming, 1.f);   // PlayAimDownAnim: PickUpGun 31 frames @30
		PlayMFSound(TEXT("WeaponSFX_HeavyMachineGun.Pickup"));
	}
}

void AMFWeapon_HeavyMachineGun::StartFire(bool bAlt)
{
	if (bAlt)
	{
		Super::StartFire(true);   // deploy / pick up
		return;
	}
	bFireHeld = true;
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerStartFire(false);
	}
	if (IsSimulatingLocally() && ZoomLevel > 0 && State == EMFWeaponState::Idle && HasAmmoInClip())
	{
		State = EMFWeaponState::Firing;
		GetWorldTimerManager().SetTimer(SpinTimer, this, &AMFWeapon_HeavyMachineGun::SpinTick, 1.f / 30.f, true);
	}
}

void AMFWeapon_HeavyMachineGun::StopFire(bool bAlt)
{
	Super::StopFire(bAlt);   // clears the held flag; SpinTick winds down
}

void AMFWeapon_HeavyMachineGun::SpinTick()
{
	const float Dt = 1.f / 30.f;
	if (bFireHeld && ZoomLevel > 0 && HasAmmoInClip())
	{
		SpinRate = FMath::Min(MaxSpin, SpinRate + SpinAccel * Dt);
		ShotAccumulator += SpinRate * RoundsPerCycle * Dt;
		while (ShotAccumulator >= 1.f && HasAmmoInClip())
		{
			ShotAccumulator -= 1.f;
			ClipAmmo -= 1;
			if (HasAuthority())
			{
				++BurstCounter;
				TraceFire(FMath::Lerp(Accuracy, AltAccuracy, SpinRate / MaxSpin), 1, false);
			}
			if (IsLocallyControlled() || GetNetMode() == NM_Standalone)
			{
				PlayFireEffects(false);
			}
		}
		return;
	}
	// Spin down (MF ZoomedIn.Tick), then stop
	SpinRate -= SpinAccel * Dt;
	if (SpinRate <= MinSpin)
	{
		SpinRate = 0.f;
		ShotAccumulator = 0.f;
		GetWorldTimerManager().ClearTimer(SpinTimer);
		State = EMFWeaponState::Idle;
		if (!HasAmmoInClip() && HasAuthority())
		{
			if (AMFCharacter* C = GetOwnerCharacter())
			{
				C->SwitchToBestWeapon();
			}
		}
	}
}

void AMFWeapon_HeavyMachineGun::ForceInactive()
{
	GetWorldTimerManager().ClearTimer(SpinTimer);
	SpinRate = 0.f;
	Super::ForceInactive();
}

FString AMFWeapon_HeavyMachineGun::GetHUDStatus() const
{
	return ZoomLevel > 0 ? FString() : TEXT("Alt-fire to deploy");
}

#undef LOCTEXT_NAMESPACE
