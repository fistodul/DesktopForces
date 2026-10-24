#include "Weapons/M4A1/M4A1.h"
#include "Core/DFContent.h"
#include "Core/DFTypes.h"

#define LOCTEXT_NAMESPACE "ServerWeapons"

AM4A1::AM4A1() {
	// Fire01-04 3 @35 + 0.05; Reload 33 @14 + 0.25
	ViewMesh = TEXT("m4a1/M4A1PickupCarryMesh");
	ArmsMesh = TEXT("M4A1PlayerMesh"); // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(-4.8f, -1.8f, -2.8f);
	RequiredContent = TEXT("Props/m4a1/M4A1PickupCarryMesh");
	bAddToLoadout = true;
	HUDIcon = TEXT("Textures/m4a1/Skins/M4A1image");
	HUDIconRect = FIntRect(0, 203, 120, 263); // WeaponIcon X=0 Y=203 W=120 H=60
	DisplayName = LOCTEXT("M4A1", "M4A1");
	InventoryGroup = 3;
	CarrySize = 2;
	MaxClipAmmo = 30;
	MaxClips = 8;
	FireTime = 0.107f;
	ZoomedFireTime = 0.143f;
	ReloadTime = 2.536f;
	EquipTime = 0.3f;
	PutDownTime = 0.25f;
	ZoomTime = 0.286f;
	HitDamage = 22.f;
	HeadshotDamage = 44.f;
	Accuracy = 0.58f;
	AltAccuracy = 0.07f;
	MaxZoomLevel = 1;
	ZoomScale = 2.f;
	RecoilPower = 0.653f;
	ClassicFireSound = TEXT("m4a1.M4A1fire");
	ClassicReloadSound = TEXT("m4a1.M4A1Reload");
	ClassicSelectSound = TEXT("AK74.AK74Select");
}

#undef LOCTEXT_NAMESPACE
