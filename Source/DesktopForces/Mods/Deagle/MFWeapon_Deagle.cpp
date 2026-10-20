#include "Mods/Deagle/MFWeapon_Deagle.h"
#include "Core/MFContent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "MFDeagle"

AMFWeapon_Deagle::AMFWeapon_Deagle()
{
	DisplayName = LOCTEXT("Deagle", "Desert Eagle");
	ViewMesh = TEXT("deagle/deaglePickupCarryMesh");
	HUDIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(MF::ClassicPath(TEXT("Textures/deagle/Icons/deagleIcon"))));
	HUDIconRect = FIntRect(0, 0, 64, 64);   // WeaponIcon X=0 Y=0 W=64 H=64
	bAddToLoadout = true;
	MaxClipAmmo = 7;            // Deagle.MaxClipAmmo
	HitDamage = 52.f;           // ProcessTraceHit
	HeadshotDamage = 120.f;
	RecoilPower = 3.f;
	AIRating = 0.5f;
	MFFireSound = TEXT("deagleSFX.deagleFire");
	MFReloadSound = TEXT("deagleSFX.deagleReload");
}

void AMFWeapon_Deagle::PostInitializeComponents()
{
	if (!MF::LoadClassic<UStaticMesh>(TEXT("Props/") + ViewMesh))
	{
		ViewMesh = GetDefault<AMFWeapon_Pistol>()->ViewMesh;   // no mod package converted: the pistol's
		HUDIcon = GetDefault<AMFWeapon_Pistol>()->HUDIcon;
		HUDIconRect = GetDefault<AMFWeapon_Pistol>()->HUDIconRect;
	}
	Super::PostInitializeComponents();
}

#undef LOCTEXT_NAMESPACE
