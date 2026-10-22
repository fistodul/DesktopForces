#include "Weapons/SC20K/SC20K.h"
#include "Core/DFContent.h"
#include "Core/DFTypes.h"

#define LOCTEXT_NAMESPACE "ServerWeapons"

ASC20K::ASC20K()
{
	// F2000.u; Fire01-04 3 @38 + 0.05; Reload 16 @5 + 0.25
	ViewMesh = TEXT("F2000/SC20KPickupCarryMesh");
	ArmsMesh = TEXT("SC20KPlayerMesh");   // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(-6.6f, -2.f, -3.3f);
	RequiredContent = TEXT("Props/F2000/SC20KPickupCarryMesh");
	bAddToLoadout = true;
	HUDIcon = TEXT("Textures/F2000/Skins/SC20Kimage");
	HUDIconRect = FIntRect(0, 3, 128, 72);   // WeaponIcon X=0 Y=3 W=128 H=69
	DisplayName = LOCTEXT("SC20K", "SC-20K");
	InventoryGroup = 6;
	SoldierAnimSet = 3;   // the mod's AnimNumber
	CarrySize = 3;
	MaxClipAmmo = 35;
	MaxClips = 8;
	FireTime = 0.103f;
	ZoomedFireTime = 0.133f;
	ReloadTime = 3.25f;
	EquipTime = 0.375f;
	PutDownTime = 0.25f;
	ZoomTime = 0.286f;
	HitDamage = 28.f;
	HeadshotDamage = 70.f;
	Accuracy = 0.545f;
	AltAccuracy = 0.056f;
	MaxZoomLevel = 1;
	ZoomScale = 2.f;
	RecoilPower = 0.63f;
	ClassicFireSound = TEXT("SC20K.sc20kFire");
	ClassicReloadSound = TEXT("SC20K.sc20kreload");
	ClassicSelectSound = TEXT("SC20K.sc20kup");
}

#undef LOCTEXT_NAMESPACE
