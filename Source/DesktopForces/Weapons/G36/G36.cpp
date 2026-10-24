#include "Weapons/G36/G36.h"
#include "Core/DFContent.h"
#include "Core/DFTypes.h"

#define LOCTEXT_NAMESPACE "ServerWeapons"

AG36::AG36() {
	// Reload @10 + 0.25
	ViewMesh = TEXT("g36/g36PickupCarryMesh");
	ArmsMesh = TEXT("g36PlayerMesh"); // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(2.5f, -1.2f, -3.5f);
	RequiredContent = TEXT("Props/g36/g36PickupCarryMesh");
	bAddToLoadout = true;
	HUDIcon = TEXT("Textures/g36/Skins/g36image");
	HUDIconRect = FIntRect(0, 190, 120, 268); // WeaponIcon X=0 Y=190 W=120 H=78
	DisplayName = LOCTEXT("G36", "G36");
	InventoryGroup = 3;
	CarrySize = 2;
	MaxClipAmmo = 30;
	MaxClips = 8;
	FireTime = 0.114f;
	ZoomedFireTime = 0.167f;
	ReloadTime = 3.05f;
	EquipTime = 0.75f;
	PutDownTime = 0.531f;
	ZoomTime = 0.5f;
	HitDamage = 24.f;
	HeadshotDamage = 48.f;
	Accuracy = 0.6f;
	AltAccuracy = 0.07f;
	MaxZoomLevel = 1;
	ZoomScale = 2.f;
	RecoilPower = 0.75f;
	ClassicFireSound = TEXT("g36.G36_Fire");
	ClassicReloadSound = TEXT("g36.g36reload");
	ClassicSelectSound = TEXT("g36.G36select");
}

#undef LOCTEXT_NAMESPACE
