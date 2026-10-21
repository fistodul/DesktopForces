#include "Weapons/Grenades/MFWeapon_Grenades.h"
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

AMFWeapon_Grenades::AMFWeapon_Grenades()
{
	ViewMesh = TEXT("RageWeapons/GrenadesPickupCarryMesh");
	HUDIconRect = FIntRect(48, 0, 96, 64);   // WeaponIcon X=48 Y=0 W=48 H=64
	DisplayName = LOCTEXT("Grenades", "Grenades");
	InventoryGroup = 8;
	CarrySize = 1;
	MaxCanCarry = 3;
	AIRating = 0.4f;
	MaxClipAmmo = 3;
	MaxClips = 1;
	bDestroyWhenEmpty = true;
	FireTime = 0.333f;         // ThrowOver 11 @30
	EquipTime = 0.5f;          // SelectUp 16 @30
	PutDownTime = 0.5f;
	bInstantHit = false;
	ProjectileClass = AMFProjectile_Grenade::StaticClass();
	AltProjectileClass = AMFProjectile_ImpactGrenade::StaticClass();
	RecoilPower = 0.f;
	MFFireSound = TEXT("WeaponSFX_Grenades.Fire");
	MFSelectSound = TEXT("WeaponSFX_Grenades.Select");
}

float AMFWeapon_Grenades::GetThrowPower() const
{
	if (!bWinding)
	{
		return 0.f;
	}
	// MF: ThrowPower += Delta * 8, max 10, min 1.5 on release
	return FMath::Clamp(8.f * float(GetWorld()->GetTimeSeconds() - WindUpStart), 1.5f, 10.f);
}

void AMFWeapon_Grenades::StartFire(bool bAlt)
{
	if (State != EMFWeaponState::Idle || !HasAmmoInClip() || bWinding)
	{
		return;
	}
	bWinding = true;
	bWindingAlt = bAlt;
	WindUpStart = GetWorld()->GetTimeSeconds();
	// PlayFiringAnim: the pin comes out with the teeth or by hand, then Powering while winding up
	PlayMFSound(FMath::RandBool() ? TEXT("WeaponSFX_Grenades.PullPinTeeth") : TEXT("WeaponSFX_Grenades.PullPinHand"));
	SetAmbientMFSound(TEXT("WeaponSFX_Grenades.Powering"));
}

void AMFWeapon_Grenades::StopFire(bool bAlt)
{
	if (!bWinding || bAlt != bWindingAlt)
	{
		return;
	}
	const float Power = GetThrowPower();
	bWinding = false;
	SetAmbientMFSound(nullptr);
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerThrow(bAlt, Power);
	}
	Throw(bAlt, Power);
}

void AMFWeapon_Grenades::ServerThrow_Implementation(bool bAlt, float Power)
{
	Throw(bAlt, FMath::Clamp(Power, 1.5f, 10.f));
}

void AMFWeapon_Grenades::Throw(bool bAlt, float Power)
{
	if (!IsSimulatingLocally() || !HasAmmoInClip())
	{
		return;
	}
	ClipAmmo -= 1;
	SetState(EMFWeaponState::Firing, FireTime);
	if (HasAuthority())
	{
		++BurstCounter;
		SpawnProjectile(bAlt ? AltProjectileClass : ProjectileClass, Power * 337.5f, bAlt); // MF ThrowPower * 150 uu/s
	}
	if (IsLocallyControlled())
	{
		PlayFireEffects(bAlt);
	}
}

FString AMFWeapon_Grenades::GetHUDStatus() const
{
	return bWinding ? FString::Printf(TEXT("THROW %d"), FMath::FloorToInt(GetThrowPower())) : FString();
}

#undef LOCTEXT_NAMESPACE
