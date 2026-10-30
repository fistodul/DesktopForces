#include "Weapons/Ultimax/Ultimax.h"
#include "Core/DFContent.h"
#include "Core/DFTypes.h"

#define LOCTEXT_NAMESPACE "ServerWeapons"

AUltimax::AUltimax() {
	// NormalFire sleeps 0.112 s (0.45 aimed); Reload 28 @8 + 0.25
	ViewMesh = TEXT("ultimax/UltimaxPickupCarryMesh");
	ArmsMesh = TEXT("UltimaxPlayerMesh"); // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(3.2f, -1.7f, -2.7f);
	RequiredContent = TEXT("Props/ultimax/UltimaxPickupCarryMesh");
	bAddToLoadout = true;
	HUDIcon = TEXT("Textures/ultimax/Skins/Ultimaximage");
	HUDIconRect = FIntRect(0, 165, 250, 235); // WeaponIcon X=0 Y=165 W=250 H=70
	DisplayName = LOCTEXT("Ultimax", "Ultimax 100");
	InventoryGroup = 6;
	SoldierAnimSet = 3; // the mod's AnimNumber
	CarrySize = 4;
	MaxClipAmmo = 100;
	MaxClips = 4;
	FireTime = 0.112f;
	ZoomedFireTime = 0.45f;
	ReloadTime = 3.625f;
	EquipTime = 1.f;
	PutDownTime = 0.933f;
	ZoomTime = 0.625f;
	HitDamage = 30.f;
	HeadshotDamage = 60.f;
	Accuracy = 0.65f;
	AltAccuracy = 0.07f;
	MaxZoomLevel = 1;
	ZoomScale = 2.f;
	ZoomedViewOffset = FVector(140.f, -92.2f, 160.f); // MF ZoomedInViewOffset
	RecoilPower = 0.85f;
	ClassicFireSound = TEXT("ultimax.ultimaxFire");
	ClassicReloadSound = TEXT("ultimax.ultimaxreload");
	ClassicSelectSound = TEXT("ultimax.ultimaxselect");
}

#undef LOCTEXT_NAMESPACE
