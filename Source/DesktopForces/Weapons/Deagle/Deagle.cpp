#include "Weapons/Deagle/Deagle.h"
#include "Core/DFContent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "DFDeagle"

ADeagle::ADeagle()
{
	DisplayName = LOCTEXT("Deagle", "Desert Eagle");
	ViewMesh = TEXT("deagle/deaglePickupCarryMesh");
	ArmsMesh = TEXT("deaglePlayerMesh");   // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.04f;
	ArmsOffset = FVector(3.f, -1.f, 0.f);
	HUDIcon = TEXT("Textures/deagle/Icons/deagleIcon");
	HUDIconRect = FIntRect(0, 0, 64, 64);   // WeaponIcon X=0 Y=0 W=64 H=64
	bAddToLoadout = true;
	MaxClipAmmo = 7;            // Deagle.MaxClipAmmo
	HitDamage = 52.f;           // ProcessTraceHit
	HeadshotDamage = 120.f;
	RecoilPower = 3.f;
	AIRating = 0.5f;
	ClassicFireSound = TEXT("deagleSFX.deagleFire");
	ClassicReloadSound = TEXT("deagleSFX.deagleReload");
}

void ADeagle::PostInitializeComponents()
{
	if (!DF::LoadClassic<UStaticMesh>(TEXT("Props/") + ViewMesh))
	{
		ViewMesh = GetDefault<APistol>()->ViewMesh;   // no mod package converted: the pistol's
		HUDIcon = GetDefault<APistol>()->HUDIcon;
		HUDIconRect = GetDefault<APistol>()->HUDIconRect;
	}
	Super::PostInitializeComponents();
}

#undef LOCTEXT_NAMESPACE
