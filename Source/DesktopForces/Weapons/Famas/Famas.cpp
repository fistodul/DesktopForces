#include "Weapons/Famas/Famas.h"
#include "Core/DFContent.h"
#include "Core/DFTypes.h"

#define LOCTEXT_NAMESPACE "ServerWeapons"

AFamas::AFamas()
{
	// Fire01-04 4 @50 + 0.05; Reload 30 @10 + 0.25
	ViewMesh = TEXT("famas/FamasPickupCarryMesh");
	RequiredContent = TEXT("Props/famas/FamasPickupCarryMesh");
	bAddToLoadout = true;
	HUDIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(FString::Printf(TEXT("%s/Textures/famas/Skins/Famasimage.Famasimage"), DF::UnverifiedRoot)));
	HUDIconRect = FIntRect(0, 194, 120, 272);   // WeaponIcon X=0 Y=194 W=120 H=78
	DisplayName = LOCTEXT("Famas", "FAMAS");
	InventoryGroup = 3;
	CarrySize = 2;
	MaxClipAmmo = 25;
	MaxClips = 10;
	FireTime = 0.11f;
	ZoomedFireTime = 0.167f;
	ReloadTime = 3.15f;
	EquipTime = 0.778f;
	PutDownTime = 0.722f;
	ZoomTime = 0.5f;
	HitDamage = 28.f;
	HeadshotDamage = 56.f;
	Accuracy = 0.58f;
	AltAccuracy = 0.07f;
	MaxZoomLevel = 1;
	ZoomScale = 2.f;
	RecoilPower = 0.98f;
	ClassicFireSound = TEXT("famas.Famas_Fire");
	ClassicReloadSound = TEXT("famas.famasreload");
	ClassicSelectSound = TEXT("famas.famasselect");
}

#undef LOCTEXT_NAMESPACE
