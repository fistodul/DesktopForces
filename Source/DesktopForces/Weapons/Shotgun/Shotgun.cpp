#include "Weapons/Shotgun/Shotgun.h"
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

AShotgun::AShotgun()
{
	ViewMesh = TEXT("RageWeapons/ShotgunPickupCarryMesh");
	HUDIconRect = FIntRect(160, 128, 256, 192);   // WeaponIcon X=160 Y=128 W=96 H=64
	DisplayName = LOCTEXT("Shotgun", "Shotgun");
	InventoryGroup = 4;
	CarrySize = 2;
	AIRating = 0.6f;
	MaxClipAmmo = 2;
	MaxClips = 20;
	FireTime = 0.472f;         // Fire*Barrel 20 frames @30 x1.5 + 0.05
	ReloadTime = 0.867f;       // Reload* 40 @30 x1.5
	EquipTime = 0.717f;
	PutDownTime = 0.383f;
	HitDamage = 12.f;
	Pellets = 10;              // NumShellFragments
	Accuracy = 0.8f;
	AltAccuracy = 1.f;
	MaxTraceDistance = 11250.f; // MF 5000
	DamageType = UDFDamage_Shotgun::StaticClass();
	RecoilPower = 0.42f;
	TracerEvery = 2;
	MuzzleFlashSize = 50.f;              // RageWeapons TracerEvery
	ClassicFireSound = TEXT("WeaponSFX_Shotgun.Fire");
	ClassicReloadSound = TEXT("WeaponSFX_Shotgun.Reload");
	ClassicSelectSound = TEXT("WeaponSFX_Shotgun.Select");
}

void AShotgun::StartFire(bool bAlt)
{
	// The other button pressed during the window turns the shot into both barrels
	if (GetWorldTimerManager().IsTimerActive(WindowTimer) && bAlt != bWindowAlt)
	{
		bTwin = true;
		(bAlt ? bAltFireHeld : bFireHeld) = true;
		if (GetLocalRole() < ROLE_Authority)
		{
			ServerStartFire(bAlt);
		}
		return;
	}
	Super::StartFire(bAlt);
}

bool AShotgun::TryFire(bool bAlt)
{
	if (!HasAmmoInClip())
	{
		return Super::TryFire(bAlt);
	}
	bWindowAlt = bAlt;
	bTwin = false;
	State = EDFWeaponState::Firing;
	GetWorldTimerManager().SetTimer(WindowTimer, this, &AShotgun::FireAfterWindow, TwinFireWindow, false);
	return true;
}

void AShotgun::FireAfterWindow()
{
	if (bTwin && ClipAmmo < 2)
	{
		bTwin = false;
	}
	SetState(EDFWeaponState::Firing, FireTime);
	FireShot(bWindowAlt);
}

void AShotgun::FireShot(bool bAlt)
{
	const int32 Barrels = bTwin ? 2 : 1;
	ClipAmmo = FMath::Max(0, ClipAmmo - Barrels);
	bLastShotAlt = bAlt;
	if (HasAuthority())
	{
		++BurstCounter;
		TraceFire(bAlt ? AltAccuracy : Accuracy, Pellets * Barrels, bAlt);
	}
	if (IsLocallyControlled() || GetNetMode() == NM_Standalone)
	{
		PlayFireEffects(bAlt);
	}
	bTwin = false;
}

void AShotgun::PlayFireEffects(bool bAlt)
{
	if (!bTwin)
	{
		Super::PlayFireEffects(bAlt);
		return;
	}
	// both barrels: TwinFireSound
	const TObjectPtr<USoundBase> Single = FireSound;
	FireSound = nullptr;
	Super::PlayFireEffects(bAlt);
	FireSound = Single;
	PlayMFSound(TEXT("WeaponSFX_Shotgun.FireBoth"));
}

#undef LOCTEXT_NAMESPACE
