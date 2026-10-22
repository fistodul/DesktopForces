#include "Weapons/HeavyMachineGun/HeavyMachineGun.h"
#include "Components/StaticMeshComponent.h"
#include "Core/DFProps.h"
#include "Player/DFSoldierAnimInstance.h"
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

AHeavyMachineGun::AHeavyMachineGun()
{
	ViewMesh = TEXT("RageWeapons/HeavyMachineGunPickupCarryMesh");
	ArmsMesh = TEXT("HeavyMachineGunPlayerMesh");   // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(3.6f, 0.f, -2.5f);
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
	ClassicFireSound = TEXT("WeaponSFX_HeavyMachineGun.Fire");
	ClassicSelectSound = TEXT("WeaponSFX_HeavyMachineGun.Select");
}

void AHeavyMachineGun::ToggleZoom()
{
	// MF OKToDeploy: only standing on the ground (not in water, not falling)
	const ADFCharacter* C = GetOwnerCharacter();
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
	if (bDeploying && State == EDFWeaponState::Zooming)
	{
		// PlayAimUpAnim / PlayReloadingAnim / PlayPullbackAnim: tripod, magazine, step back
		PlayMFSound(TEXT("WeaponSFX_HeavyMachineGun.SetupTripod"));
		PlayArms(TEXT("ActivateGun01"), 1.3f);
		GetWorldTimerManager().SetTimer(DeploySoundTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			PlayMFSound(TEXT("WeaponSFX_HeavyMachineGun.SetupMagazine"));
			PlayArms(TEXT("ActivateGun02"), 1.f);
			GetWorldTimerManager().SetTimer(DeploySoundTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				PlayMFSound(TEXT("WeaponSFX_HeavyMachineGun.SetupStepBack"));
				PlayArms(TEXT("ActivateGun03"), 2.f);
			}), 1.f, false);
		}), 1.026f, false);
	}
	else if (!bDeploying)
	{
		SetState(EDFWeaponState::Zooming, 1.f);   // PlayAimDownAnim: PickUpGun 31 frames @30
		PlayMFSound(TEXT("WeaponSFX_HeavyMachineGun.Pickup"));
	}
}

void AHeavyMachineGun::StartFire(bool bAlt)
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
	if (IsSimulatingLocally() && ZoomLevel > 0 && State == EDFWeaponState::Idle && HasAmmoInClip())
	{
		State = EDFWeaponState::Firing;
		GetWorldTimerManager().SetTimer(SpinTimer, this, &AHeavyMachineGun::SpinTick, 1.f / 30.f, true);
	}
}

void AHeavyMachineGun::StopFire(bool bAlt)
{
	Super::StopFire(bAlt);   // clears the held flag; SpinTick winds down
}

void AHeavyMachineGun::SpinTick()
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
		State = EDFWeaponState::Idle;
		if (!HasAmmoInClip() && HasAuthority())
		{
			if (ADFCharacter* C = GetOwnerCharacter())
			{
				C->SwitchToBestWeapon();
			}
		}
	}
}

void AHeavyMachineGun::ForceInactive()
{
	GetWorldTimerManager().ClearTimer(SpinTimer);
	SpinRate = 0.f;
	Super::ForceInactive();
}

void AHeavyMachineGun::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	Tripod3P = NewObject<UStaticMeshComponent>(this, TEXT("Tripod3P"));
	Tripod3P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Tripod3P->SetOwnerNoSee(true);   // the owner sees the first-person gun on its tripod
	Tripod3P->bCastDynamicShadow = false;
	Tripod3P->SetUsingAbsoluteLocation(true);
	Tripod3P->SetUsingAbsoluteRotation(true);
	Tripod3P->SetVisibility(false);
	Tripod3P->SetupAttachment(RootComponent);
	Tripod3P->RegisterComponent();
	if (!DF::ApplyPropMesh(Tripod3P, TEXT("RageWeapons/TripodMeshTemp")))
	{
		Tripod3P->DestroyComponent();
		Tripod3P = nullptr;
	}
}

void AHeavyMachineGun::OnZoomChanged()
{
	const ADFCharacter* C = GetOwnerCharacter();
	if (!Tripod3P)
	{
		return;
	}
	if (!C || !IsDeployed())
	{
		Tripod3P->SetVisibility(false);
		return;
	}
	// HeavyMachineGun.ZoomedIn: the tripod 28 uu under the gun's pivot, on the ground in front
	// of the kneeling soldier, turned his way
	const float Feet = C->GetActorLocation().Z - C->GetSimpleCollisionHalfHeight();
	const FVector Gun = C->GetActorLocation() + C->GetActorForwardVector() * 45.f;
	Tripod3P->SetWorldLocationAndRotation(FVector(Gun.X, Gun.Y, Feet), FRotator(0.f, C->GetActorRotation().Yaw, 0.f));
	Tripod3P->SetVisibility(true);
}

void AHeavyMachineGun::PlayArms(FName Name, float Rate)
{
	if (UDFTweenAnimInstance* Anim = IsLocallyControlled() ? GetArmsAnim() : nullptr)
	{
		Anim->SetIdle(IsDeployed() ? TEXT("ActivateStill") : TEXT("Still"));
		Anim->Play(Name, Rate, 0.05f, false);
	}
}

FName AHeavyMachineGun::GetArmsAnimName(EDFWeaponState ForState, bool bAlt)
{
	// HeavyMachineGun: ActivateGun01-03 setting up (played with the deploy's sounds), PickUpGun
	// packing up, ActivateStill on the tripod; the firing animation runs at rate 0.01 in MF:
	// the gun just shakes with the shots
	switch (ForState)
	{
	case EDFWeaponState::Zooming: return IsDeployed() ? NAME_None : FName(TEXT("PickupGun"));
	case EDFWeaponState::Firing:  return NAME_None;
	case EDFWeaponState::Idle:    return IsDeployed() ? FName(TEXT("ActivateStill")) : FName(TEXT("Still"));
	default:                      return Super::GetArmsAnimName(ForState, bAlt);
	}
}

FString AHeavyMachineGun::GetHUDStatus() const
{
	return ZoomLevel > 0 ? FString() : TEXT("Alt-fire to deploy");
}

#undef LOCTEXT_NAMESPACE
