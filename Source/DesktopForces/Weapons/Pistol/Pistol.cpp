#include "Weapons/Pistol/Pistol.h"
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

APistol::APistol()
{
	ViewMesh = TEXT("RageWeapons/PistolPickupCarryMesh");
	ArmsMesh = TEXT("PistolPlayerMesh");   // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(3.f, -1.f, 0.f);
	HUDIconRect = FIntRect(0, 0, 48, 64);   // WeaponIcon X=0 Y=0 W=48 H=64
	DisplayName = LOCTEXT("Pistol", "Pistol");
	InventoryGroup = 2;
	CarrySize = 1;
	AIRating = 0.45f;
	MaxClipAmmo = 8;
	MaxClips = 6;
	FireTime = 0.342f;         // Fire 15 frames @48 + 0.05 tween
	ZoomedFireTime = 0.5f;     // AccurateAimFire 16 @30
	ReloadTime = 1.55f;        // Reload 40 @30 + 0.25 tween
	EquipTime = 0.667f;        // SelectUp 21 @30
	PutDownTime = 0.7f;        // SelectDown 22 @30
	ZoomTime = 0.333f;         // AccurateAimUp 11 @30
	HitDamage = 26.f;
	HeadshotDamage = 40.f;
	Accuracy = 0.4f;
	AltAccuracy = 0.05f;
	MaxZoomLevel = 1;
	ZoomScale = 1.5f;
	RecoilPower = 1.f;
	TracerEvery = 1;
	MuzzleFlashSize = 30.f;
	CasingMesh = TEXT("RageWeapons/PistolBullet");   // EjectBrass
	CasingScale = 1.f;              // RageWeapons TracerEvery
	ClassicFireSound = TEXT("WeaponSFX_Pistol.Fire");
	ClassicReloadSound = TEXT("WeaponSFX_Pistol.Reload");
	ClassicSelectSound = TEXT("WeaponSFX_Pistol.Select");
}

#undef LOCTEXT_NAMESPACE
