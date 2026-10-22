#include "Weapons/WA2000/WA2000.h"
#include "Core/DFContent.h"
#include "Core/DFTypes.h"

#define LOCTEXT_NAMESPACE "ServerWeapons"

AWA2000::AWA2000()
{
	// head 10000000 (one shot); scope 3x - 30x in 6 steps in MF: a fixed 6x here
	ViewMesh = TEXT("wa2000/wa2000PickupCarryMesh");
	ArmsMesh = TEXT("wa2000PlayerMesh");   // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(1.f, 0.5f, -3.5f);
	RequiredContent = TEXT("Props/wa2000/wa2000PickupCarryMesh");
	bAddToLoadout = true;
	HUDIcon = TEXT("Textures/wa2000/Skins/wa2000image");
	HUDIconRect = FIntRect(0, 6, 128, 75);   // WeaponIcon X=0 Y=6 W=128 H=69
	DisplayName = LOCTEXT("WA2000", "WA 2000");
	InventoryGroup = 5;
	CarrySize = 3;
	MaxClipAmmo = 6;
	MaxClips = 5;
	FireTime = 0.606f;
	ZoomedFireTime = 0.556f;
	ReloadTime = 3.679f;
	EquipTime = 0.8f;
	PutDownTime = 0.8f;
	ZoomTime = 0.75f;
	HitDamage = 60.f;
	HeadshotDamage = 10000.f;
	Accuracy = 0.3f;
	AltAccuracy = 0.f;
	MaxZoomLevel = 1;
	ZoomScale = 6.f;
	RecoilPower = 3.5f;
	MaxTraceDistance = 45000.f;
	ZoomedMoveScale = 0.f;
	CasingMesh = TEXT("RageWeapons/RifleBullet");   // RageRifle_BulletCase
	MuzzleFlashSize = 40.f;
	ClassicFireSound = TEXT("wa2000.wa2000_Fire");
	ClassicReloadSound = TEXT("wa2000.wa2000_reload");
	ClassicSelectSound = TEXT("WeaponSFX_SniperRifle.Select");
}

#undef LOCTEXT_NAMESPACE
