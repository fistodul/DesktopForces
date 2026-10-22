#include "Weapons/MAC10/MAC10.h"
#include "Core/DFContent.h"
#include "Core/DFTypes.h"

#define LOCTEXT_NAMESPACE "ServerWeapons"

AMAC10::AMAC10()
{
	// Fire01-04 3 @55 + 0.05; Reload 10 @8 + 0.25
	ViewMesh = TEXT("MAC10/MAC10PickupCarryMesh");
	ArmsMesh = TEXT("MAC10PlayerMesh");   // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(-3.f, 0.5f, -5.5f);
	RequiredContent = TEXT("Props/MAC10/MAC10PickupCarryMesh");
	bAddToLoadout = true;
	HUDIcon = TEXT("Textures/MAC10/Skins/MAC10image");
	HUDIconRect = FIntRect(0, 162, 120, 240);   // WeaponIcon X=0 Y=162 W=120 H=78
	DisplayName = LOCTEXT("MAC10", "MAC-10");
	InventoryGroup = 2;
	CarrySize = 1;
	MaxClipAmmo = 30;
	MaxClips = 8;
	FireTime = 0.086f;
	ZoomedFireTime = 0.125f;
	ReloadTime = 1.375f;
	EquipTime = 0.4f;
	PutDownTime = 0.4f;
	ZoomTime = 0.5f;
	HitDamage = 22.f;
	HeadshotDamage = 44.f;
	Accuracy = 0.76f;
	AltAccuracy = 0.1f;
	MaxZoomLevel = 1;
	ZoomScale = 2.f;
	RecoilPower = 1.f;
	ClassicFireSound = TEXT("MAC10.MAC10Fire");
	ClassicReloadSound = TEXT("MAC10.MAC10Reload");
	ClassicSelectSound = TEXT("MAC10.MAC10Pickup");
}

#undef LOCTEXT_NAMESPACE
