#include "Weapons/Rifle/MFWeapon_Rifle.h"
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

AMFWeapon_Rifle::AMFWeapon_Rifle()
{
	ViewMesh = TEXT("RageWeapons/SniperRiflePickupCarryMesh");
	HUDIconRect = FIntRect(104, 192, 248, 256);   // WeaponIcon X=104 Y=192 W=144 H=64
	DisplayName = LOCTEXT("Rifle", "Sniper Rifle");
	InventoryGroup = 5;
	CarrySize = 3;
	AIRating = 0.5f;
	MaxClipAmmo = 4;
	MaxClips = 4;
	FireTime = 0.55f;          // Fire 16 @30 + 0.05
	ZoomedFireTime = 0.55f;
	ReloadTime = 1.383f;       // Reload 41 @30 + 0.05
	EquipTime = 0.667f;        // SelectUp at rate 0.5
	PutDownTime = 0.667f;
	ZoomTime = 0.667f;         // BringScopeUp at rate 0.5
	HitDamage = 20.f;          // x ShotPower
	Accuracy = 0.3f;
	AltAccuracy = 0.f;
	MaxTraceDistance = 45000.f; // MF 20000
	MaxZoomLevel = 1;
	ZoomScale = 3.f;
	ZoomedMoveScale = 0.f;     // can't walk while scoped (MF: "no use to players on the move")
	RecoilPower = 1.f;
	MFFireSound = TEXT("WeaponSFX_SniperRifle.Fire");
	MFReloadSound = TEXT("WeaponSFX_SniperRifle.Reload");
	MFSelectSound = TEXT("WeaponSFX_SniperRifle.Select");
}

void AMFWeapon_Rifle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AMFWeapon_Rifle, ZoomScaleAlpha, COND_OwnerOnly);
}

float AMFWeapon_Rifle::GetShotPower() const
{
	if (!bCharging)
	{
		return ReleasedPower;
	}
	// MF: ShotPower += Delta * 2, from 1 to 10
	return FMath::Clamp(1.f + 2.f * float(GetWorld()->GetTimeSeconds() - ChargeStart), 1.f, 10.f);
}

void AMFWeapon_Rifle::StartFire(bool bAlt)
{
	if (!bAlt && State == EMFWeaponState::Idle && HasAmmoInClip())
	{
		bCharging = true;
		ChargeStart = GetWorld()->GetTimeSeconds();
		bFireHeld = true;
		// RageRifle.ClientFire: AmbientSound Powering until ShotPower hits 10 (4.5 s), then PoweredUp
		SetAmbientMFSound(TEXT("WeaponSFX_SniperRifle.Powering"));
		GetWorldTimerManager().SetTimer(PoweredUpTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			SetAmbientMFSound(nullptr);
			PlayMFSound(TEXT("WeaponSFX_SniperRifle.PoweredUp"));
		}), 4.5f, false);
		return;   // fires on release
	}
	Super::StartFire(bAlt);
}

void AMFWeapon_Rifle::StopFire(bool bAlt)
{
	if (!bAlt && bCharging)
	{
		ReleasedPower = GetShotPower();
		bCharging = false;
		bFireHeld = false;
		GetWorldTimerManager().ClearTimer(PoweredUpTimer);
		SetAmbientMFSound(nullptr);
		if (IsLocallyControlled() && GetLocalRole() < ROLE_Authority)
		{
			ServerReleaseFire(ReleasedPower);
		}
		if (IsSimulatingLocally())
		{
			TryFire(false);
		}
		return;
	}
	Super::StopFire(bAlt);
}

void AMFWeapon_Rifle::ServerReleaseFire_Implementation(float Power)
{
	ReleasedPower = FMath::Clamp(Power, 1.f, 10.f);
	bCharging = false;
	TryFire(false);
}

void AMFWeapon_Rifle::FireShot(bool bAlt)
{
	Super::FireShot(bAlt);
	RecoilPower = ReleasedPower * 0.5f;   // MF: RecoilPower = ShotPower * 0.5
}

void AMFWeapon_Rifle::Finish()
{
	// Never auto-refire: every shot is charged
	const bool bHeld = bFireHeld;
	bFireHeld = false;
	Super::Finish();
	bFireHeld = bHeld;
}

void AMFWeapon_Rifle::ProcessTraceHit(const FHitResult& Hit, const FVector& ShotDir, bool bAlt)
{
	const float SavedDamage = HitDamage;
	const float SavedMomentum = HitMomentum;
	HitDamage = 20.f * ReleasedPower;                       // GetShotDamage
	HeadshotDamage = HitDamage * 3.f;
	HitMomentum = 11250.f * ReleasedPower + 11250.f;        // GetShotMomentum: 5000 * power + 5000 uu
	Super::ProcessTraceHit(Hit, ShotDir, bAlt);
	HitDamage = SavedDamage;
	HitMomentum = SavedMomentum;
}

float AMFWeapon_Rifle::GetFOVScale() const
{
	if (ZoomLevel == 0)
	{
		return 1.f;
	}
	// MF: ZoomScale = Min + (Max - Min) * alpha^2
	const float Scale = MinZoomScale + (MaxZoomScale - MinZoomScale) * ZoomScaleAlpha * ZoomScaleAlpha;
	return 1.f / Scale;
}

void AMFWeapon_Rifle::AdjustZoom(int32 Steps)
{
	if (ZoomLevel == 0)
	{
		return;
	}
	ZoomScaleAlpha = FMath::Clamp(ZoomScaleAlpha + float(Steps) / float(ZoomScaleIncrements), 0.f, 1.f);
}

FString AMFWeapon_Rifle::GetHUDStatus() const
{
	return bCharging ? FString::Printf(TEXT("POWER %d"), FMath::FloorToInt(GetShotPower())) : FString();
}

#undef LOCTEXT_NAMESPACE
