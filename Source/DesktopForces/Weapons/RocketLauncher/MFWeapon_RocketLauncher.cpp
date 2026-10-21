#include "Weapons/RocketLauncher/MFWeapon_RocketLauncher.h"
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

AMFWeapon_RocketLauncher::AMFWeapon_RocketLauncher()
{
	ViewMesh = TEXT("RageWeapons/RocketLauncherPickupCarryMesh");
	HUDIconRect = FIntRect(0, 128, 160, 192);   // WeaponIcon X=0 Y=128 W=160 H=64
	DisplayName = LOCTEXT("RocketLauncher", "Rocket Launcher");
	InventoryGroup = 7;
	CarrySize = 4;
	AIRating = 0.6f;
	MaxClipAmmo = 1;
	MaxClips = 11;
	FireTime = 0.79f;          // Fire 21 @30 x0.9 + 0.05
	ZoomedFireTime = 0.79f;
	ReloadTime = 3.38f;        // Reload 41 @30 x0.4 + 0.05
	EquipTime = 0.667f;
	PutDownTime = 0.526f;      // SelectDown 21 @38
	ZoomTime = 1.333f;         // BringScopeUp 21 @30 x0.5
	bInstantHit = false;
	ProjectileClass = AMFProjectile_Rocket::StaticClass();
	AltProjectileClass = AMFProjectile_Rocket::StaticClass();
	MaxZoomLevel = 1;
	ZoomScale = 4.f;
	RecoilPower = 8.f;
	MFFireSound = TEXT("WeaponSFX_RocketLauncher.Fire");
	MFReloadSound = TEXT("WeaponSFX_RocketLauncher.Reload");
	MFSelectSound = TEXT("WeaponSFX_RocketLauncher.Select");
}

#undef LOCTEXT_NAMESPACE
