#include "Weapons/M87/M87.h"
#include "Core/DFContent.h"
#include "Core/DFTypes.h"

#define LOCTEXT_NAMESPACE "ServerWeapons"

AM87::AM87()
{
	// lever action (extends Pistol in MF: 8 shells); Fire 9 @35 x0.5..; Reload 2.25 s empty
	ViewMesh = TEXT("m87/M87PickupCarryMesh");
	ArmsMesh = TEXT("M87PlayerMesh");   // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(-5.3f, -2.f, -4.2f);
	RequiredContent = TEXT("Props/m87/M87PickupCarryMesh");
	bAddToLoadout = true;
	HUDIcon = TEXT("Textures/m87/Skins/M87image");
	HUDIconRect = FIntRect(0, 196, 128, 266);   // WeaponIcon X=0 Y=196 W=128 H=70
	DisplayName = LOCTEXT("M87", "M1887");
	InventoryGroup = 4;
	SoldierAnimSet = 5;   // the mod's AnimNumber
	CarrySize = 3;
	MaxClipAmmo = 8;
	MaxClips = 8;
	FireTime = 1.193f;
	ZoomedFireTime = 0.819f;
	ReloadTime = 2.25f;
	EquipTime = 0.5f;
	PutDownTime = 0.5f;
	ZoomTime = 0.333f;
	HitDamage = 14.f;
	HeadshotDamage = 28.f;
	Accuracy = 1.3f;
	AltAccuracy = 0.7f;
	RecoilPower = 0.6f;
	Pellets = 12;               // NumShellFragments
	TracerEvery = 2;            // the shotgun's
	MaxTraceDistance = 11250.f; // the shotgun's
	MaxZoomLevel = 0;
	DamageType = UDFDamage_Shotgun::StaticClass();
	MuzzleFlashSize = 50.f;
	ClassicFireSound = TEXT("m871.m87fire");
	ClassicReloadSound = TEXT("m871.m87reload");
	ClassicSelectSound = TEXT("WeaponSFX_Shotgun.Select");
}

#undef LOCTEXT_NAMESPACE
