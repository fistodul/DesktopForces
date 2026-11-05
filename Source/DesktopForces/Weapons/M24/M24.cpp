#include "Weapons/M24/M24.h"
#include "Core/DFContent.h"
#include "Core/DFTypes.h"

#define LOCTEXT_NAMESPACE "ServerWeapons"

AM24::AM24() {
	DeathMessage = NSLOCTEXT("DFKills", "M24", "%k fucilated %o .");
	// bolt action: Fire 17 @10 + 0.05; Reload 45 @15 + 0.25
	ViewMesh = TEXT("M24/M24PickupCarryMesh");
	ArmsMesh = TEXT("M24PlayerMesh"); // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(-7.3f, -0.9f, -2.3f);
	RequiredContent = TEXT("Props/M24/M24PickupCarryMesh");
	bAddToLoadout = true;
	HUDIcon = TEXT("Textures/M24/Skins/M24image");
	HUDIconRect = FIntRect(0, 196, 128, 266); // WeaponIcon X=0 Y=196 W=128 H=70
	DisplayName = LOCTEXT("M24", "M24");
	InventoryGroup = 5;
	CarrySize = 3;
	MaxClipAmmo = 8;
	MaxClips = 6;
	FireTime = 1.65f;
	ZoomedFireTime = 1.857f;
	ReloadTime = 3.183f;
	EquipTime = 0.25f;
	PutDownTime = 0.25f;
	ZoomTime = 0.333f;
	HitDamage = 80.f;
	HeadshotDamage = 180.f;
	Accuracy = 0.1f;
	AltAccuracy = 0.05f;
	MaxZoomLevel = 1;
	ZoomScale = 2.f;
	ZoomedViewOffset = FVector(450.f, -80.95f, 156.5f); // MF ZoomedInViewOffset
	RecoilPower = 4.0f;
	MaxTraceDistance = 45000.f; // like the sniper rifle
	CasingMesh = TEXT("RageWeapons/PistolBullet"); // EjectBrass: Pistol_BulletCase at the hip (the scope hides the rifle case)
	MuzzleFlashSize = 40.f;
	// the mod kept the pistol's numbers, which sit on this long gun's body: at the barrel instead
	FlashY = 0.055f;
	FlashO = -0.0115f;
	FlashS = 0.f;
	FlashYZoom = 1000.1f; // MF 0.1 sits on the aimed rifle's scope; the barrel is behind it: off screen, like the Glock mod
	ClassicFireSound = TEXT("M24.M24Fire");
	ClassicReloadSound = TEXT("M24.M24Reload");
	ClassicSelectSound = TEXT("M24.M24Select");
}

#undef LOCTEXT_NAMESPACE
