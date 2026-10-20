#include "Weapons/MFWeapon_MachineGun.h"
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

AMFWeapon_MachineGun::AMFWeapon_MachineGun()
{
	ViewMesh = TEXT("RageWeapons/MachineGunPickupCarryMesh");
	HUDIconRect = FIntRect(0, 192, 104, 256);   // WeaponIcon X=0 Y=192 W=104 H=64
	DisplayName = LOCTEXT("MachineGun", "Machine Gun");
	InventoryGroup = 3;
	CarrySize = 2;
	AIRating = 0.7f;
	MaxClipAmmo = 48;
	MaxClips = 5;
	FireTime = 0.117f;         // ContinuousFire 3 frames @30 + 0.05 tween
	ZoomedFireTime = 0.333f;   // AccurateAimFire 11 @30
	ReloadTime = 2.25f;        // Reload 41 @20 + 0.25
	EquipTime = 0.333f;
	PutDownTime = 0.333f;
	ZoomTime = 0.333f;
	HitDamage = 20.f;
	Accuracy = 0.7f;
	AltAccuracy = 0.1f;
	MaxZoomLevel = 1;
	ZoomScale = 2.f;
	RecoilPower = 0.8f;
	MFFireSound = TEXT("WeaponSFX_MachineGun.Fire");
	MFReloadSound = TEXT("WeaponSFX_MachineGun.Reload");
	MFSelectSound = TEXT("WeaponSFX_MachineGun.Select");
}

#undef LOCTEXT_NAMESPACE
