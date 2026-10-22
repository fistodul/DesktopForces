#include "Weapons/DesertEagle/DesertEagle.h"
#include "Core/DFContent.h"
#include "Core/DFTypes.h"

#define LOCTEXT_NAMESPACE "ServerWeapons"

ADesertEagle::ADesertEagle()
{
	// desert.u; Fire 5 @16 + 0.05; Reload 35 @14 + 0.25
	ViewMesh = TEXT("desert/DesertPickupCarryMesh");
	ArmsMesh = TEXT("DesertPlayerMesh");   // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(1.f, -1.4f, -2.4f);
	RequiredContent = TEXT("Props/desert/DesertPickupCarryMesh");
	bAddToLoadout = true;
	HUDIcon = TEXT("Textures/desert/Skins/Desertimage");
	HUDIconRect = FIntRect(0, 178, 120, 256);   // WeaponIcon X=0 Y=178 W=120 H=78
	DisplayName = LOCTEXT("DesertEagle", "Desert Eagle");
	InventoryGroup = 2;
	CarrySize = 1;
	MaxClipAmmo = 12;
	MaxClips = 5;
	FireTime = 0.3f;
	ZoomedFireTime = 0.231f;
	ReloadTime = 2.679f;
	EquipTime = 0.75f;
	PutDownTime = 0.625f;
	ZoomTime = 0.5f;
	HitDamage = 25.f;
	HeadshotDamage = 70.f;
	Accuracy = 0.22f;
	AltAccuracy = 0.05f;
	MaxZoomLevel = 1;
	ZoomScale = 1.5f;
	RecoilPower = 0.9f;
	ClassicFireSound = TEXT("Desert.Desert_Fire");
	ClassicReloadSound = TEXT("Desert.desertReload");
	ClassicSelectSound = TEXT("Desert.desertSelect");
}

#undef LOCTEXT_NAMESPACE
