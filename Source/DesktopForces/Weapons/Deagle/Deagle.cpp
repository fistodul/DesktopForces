#include "Weapons/Deagle/Deagle.h"
#include "Core/DFContent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Player/DFSoldierAnimInstance.h"
#include "Sound/SoundBase.h"

#define LOCTEXT_NAMESPACE "DFDeagle"

ADeagle::ADeagle() {
	DisplayName = LOCTEXT("Deagle", "Desert Eagle");
	ViewMesh = TEXT("deagle/deaglePickupCarryMesh");
	ArmsMesh = TEXT("deaglePlayerMesh"); // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	MuzzleBone = TEXT("pistoletas");
	MuzzleOffset = FVector(-35.19f, -420.13f, 289.02f);
	ArmsScale = 0.04f;
	ArmsOffset = FVector(3.f, -1.f, -5.f); // inherits from Weapon.uc via Pistol
	ArmsRoll = 0; // deagle.uc: Rotation=(Roll=0)
	HUDIcon = TEXT("Textures/deagle/Icons/deagleIcon");
	HUDIconRect = FIntRect(0, 0, 64, 64); // WeaponIcon X=0 Y=0 W=64 H=64
	bAddToLoadout = true;
	MaxClipAmmo = 7; // Deagle.MaxClipAmmo
	HitDamage = 52.f; // ProcessTraceHit
	HeadshotDamage = 120.f;
	RecoilPower = 3.f;
	AIRating = 0.5f;
	ClassicFireSound = TEXT("deagleSFX.deagleFire");
	ClassicReloadSound = TEXT("deagleSFX.deagleReload");
}

void ADeagle::PostInitializeComponents() {
	if (!DF::LoadClassic<UStaticMesh>(TEXT("Props/") + ViewMesh)) {
		ViewMesh = GetDefault<APistol>()->ViewMesh; // no mod package converted: the pistol's
		HUDIcon = GetDefault<APistol>()->HUDIcon;
		HUDIconRect = GetDefault<APistol>()->HUDIconRect;
	}
	Super::PostInitializeComponents();
}

FName ADeagle::GetArmsAnimName(EDFWeaponState ForState, bool bAlt) {
	UDFTweenAnimInstance* Anim = GetArmsAnim();
	if (Anim && ClipAmmo == 0) {
		// Fire: if (ClipAmmo == 0) PlayEmptyFiringAnim / PlayEmptyAltFiringAnim; ClientReload:
		// ClipAmmo == 0 -> PlayEmptyReloadingAnim
		const TCHAR* Empty = ForState == EDFWeaponState::Firing ? (IsZoomed() ? TEXT("AccurateAimFireEmpty") : TEXT("ShootEmpty"))
			: ForState == EDFWeaponState::Reloading             ? TEXT("ReloadEmpty")
																: nullptr;
		if (Empty && Anim->HasSequence(Empty))
			return Empty;
	}
	return Super::GetArmsAnimName(ForState, bAlt);
}

USoundBase* ADeagle::GetReloadSound() const {
	if (ClipAmmo == 0) {
		if (!EmptyReloadSound)
			const_cast<ADeagle*>(this)->EmptyReloadSound = DF::LoadSound(TEXT("deagleSFX.deagleEmptyReload"));
		if (EmptyReloadSound)
			return EmptyReloadSound;
	}
	return Super::GetReloadSound();
}

#undef LOCTEXT_NAMESPACE
