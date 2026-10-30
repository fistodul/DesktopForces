#include "Weapons/MachineGun/MachineGun.h"
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

AMachineGun::AMachineGun() {
	ViewMesh = TEXT("RageWeapons/MachineGunPickupCarryMesh");
	ArmsMesh = TEXT("MachineGunPlayerMesh"); // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(2.2f, -1.f, -5.f); // MF PlayerViewOffset Z inherits from Weapon.uc
	HUDIconRect = FIntRect(0, 192, 104, 256); // WeaponIcon X=0 Y=192 W=104 H=64
	DisplayName = LOCTEXT("MachineGun", "Machine Gun");
	InventoryGroup = 3;
	CarrySize = 2;
	AIRating = 0.7f;
	MaxClipAmmo = 48;
	MaxClips = 5;
	FireTime = 0.117f; // ContinuousFire 3 frames @30 + 0.05 tween
	ZoomedFireTime = 0.333f; // AccurateAimFire 11 @30
	ReloadTime = 2.25f; // Reload 41 @20 + 0.25
	EquipTime = 0.333f;
	PutDownTime = 0.333f;
	ZoomTime = 0.333f;
	HitDamage = 20.f;
	Accuracy = 0.7f;
	AltAccuracy = 0.1f;
	MaxZoomLevel = 1;
	ZoomScale = 2.f;
	ZoomedViewOffset = FVector(240.f, -50.f, 180.f); // MF ZoomedInViewOffset
	RecoilPower = 0.8f;
	TracerEvery = 5;
	MuzzleFlashSize = 40.f;
	CasingMesh = TEXT("RageWeapons/MachineGunBullet"); // EjectBrass
	CasingScale = 0.7f; // RageWeapons TracerEvery
	ClassicFireSound = TEXT("WeaponSFX_MachineGun.Fire");
	ClassicReloadSound = TEXT("WeaponSFX_MachineGun.Reload");
	ClassicSelectSound = TEXT("WeaponSFX_MachineGun.Select");
}

#undef LOCTEXT_NAMESPACE
