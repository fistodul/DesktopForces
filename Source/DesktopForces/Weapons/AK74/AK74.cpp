#include "Weapons/AK74/AK74.h"
#include "Core/DFContent.h"
#include "Core/DFTypes.h"

#define LOCTEXT_NAMESPACE "ServerWeapons"

AAK74::AAK74() {
	// Fire01-04 3 frames @26 + 0.05; AccurateAimFire 6 @30; Reload 10 @6 + 0.25; SelectUp 3 @8
	ViewMesh = TEXT("AK74/AK74PickupCarryMesh");
	ArmsMesh = TEXT("AK74PlayerMesh"); // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(-3.4f, -0.7f, -2.f);
	RequiredContent = TEXT("Props/AK74/AK74PickupCarryMesh");
	bAddToLoadout = true;
	HUDIcon = TEXT("Textures/AK74/Skins/AK74image");
	HUDIconRect = FIntRect(0, 194, 120, 272); // WeaponIcon X=0 Y=194 W=120 H=78
	DisplayName = LOCTEXT("AK74", "AK-74");
	InventoryGroup = 3;
	CarrySize = 2;
	MaxClipAmmo = 30;
	MaxClips = 8;
	FireTime = 0.127f;
	ZoomedFireTime = 0.167f;
	ReloadTime = 1.75f;
	EquipTime = 0.25f;
	PutDownTime = 0.25f;
	ZoomTime = 0.286f;
	HitDamage = 25.f;
	HeadshotDamage = 50.f;
	Accuracy = 0.6f;
	AltAccuracy = 0.078f;
	MaxZoomLevel = 1;
	ZoomScale = 2.f;
	ZoomedViewOffset = FVector(240.f, -6.75f, 100.f); // MF ZoomedInViewOffset
	RecoilPower = 0.82f;
	ClassicFireSound = TEXT("AK74.AK74Fire");
	ClassicReloadSound = TEXT("AK74.AK74Reload");
	ClassicSelectSound = TEXT("AK74.AK74Select");
}

#undef LOCTEXT_NAMESPACE
