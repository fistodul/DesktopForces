#include "Weapons/Glock/Glock.h"
#include "Core/DFContent.h"
#include "Core/DFTypes.h"

#define LOCTEXT_NAMESPACE "ServerWeapons"

AGlock::AGlock()
{
	// Fire 8 @30 + 0.05; aiming (no magnification) fires AccurateAimFire 5 @85: full auto, wild
	ViewMesh = TEXT("glock/glockPickupCarryMesh");
	ArmsMesh = TEXT("glockPlayerMesh");   // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(0.2f, -0.8f, -2.2f);
	RequiredContent = TEXT("Props/glock/glockPickupCarryMesh");
	bAddToLoadout = true;
	HUDIcon = TEXT("Textures/glock/Skins/glockimage");
	HUDIconRect = FIntRect(0, 193, 120, 271);   // WeaponIcon X=0 Y=193 W=120 H=78
	DisplayName = LOCTEXT("Glock", "Glock 18");
	InventoryGroup = 2;
	CarrySize = 1;
	MaxClipAmmo = 30;
	MaxClips = 4;
	FireTime = 0.283f;
	ZoomedFireTime = 0.097f;
	ReloadTime = 2.788f;
	EquipTime = 1.f;
	PutDownTime = 0.267f;
	ZoomTime = 0.625f;
	HitDamage = 28.f;
	HeadshotDamage = 40.f;
	Accuracy = 0.05f;
	AltAccuracy = 0.5f;
	MaxZoomLevel = 1;
	ZoomScale = 1.f;
	RecoilPower = 0.75f;
	ClassicFireSound = TEXT("Glock.Glock_Fire");
	ClassicReloadSound = TEXT("Glock.Glock_Reload");
	ClassicSelectSound = TEXT("Glock.Glock_Select");
}

#undef LOCTEXT_NAMESPACE
