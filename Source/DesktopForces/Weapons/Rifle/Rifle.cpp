#include "Weapons/Rifle/Rifle.h"
#include "Weapons/DFProjectile.h"
#include "Weapons/TripBombs/TripBomb.h"
#include "Player/DFCharacterMovement.h"
#include "Kismet/GameplayStatics.h"
#include "Player/DFCharacter.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "DFWeapons"

ARifle::ARifle() {
	ViewMesh = TEXT("RageWeapons/SniperRiflePickupCarryMesh");
	ArmsMesh = TEXT("SniperRiflePlayerMesh"); // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(3.6f, 0.2f, -3.5f);
	HUDIconRect = FIntRect(104, 192, 248, 256); // WeaponIcon X=104 Y=192 W=144 H=64
	DisplayName = LOCTEXT("Rifle", "Sniper Rifle");
	InventoryGroup = 5;
	CarrySize = 3;
	AIRating = 0.5f;
	MaxClipAmmo = 4;
	MaxClips = 4;
	FireTime = 0.55f; // Fire 16 @30 + 0.05
	ZoomedFireTime = 0.55f;
	ReloadTime = 1.383f; // Reload 41 @30 + 0.05
	EquipTime = 0.667f; // SelectUp at rate 0.5
	PutDownTime = 0.667f;
	ZoomTime = 0.667f; // BringScopeUp at rate 0.5
	HitDamage = 20.f; // x ShotPower
	Accuracy = 0.3f;
	AltAccuracy = 0.f;
	MaxTraceDistance = 45000.f; // MF 20000
	MaxZoomLevel = 1;
	ZoomScale = 3.f;
	ZoomedMoveScale = 0.f; // can't walk while scoped (MF: "no use to players on the move")
	RecoilPower = 1.f;
	MuzzleFlashSize = 40.f;
	CasingMesh = TEXT("RageWeapons/RifleBullet"); // EjectBrass
	CasingScale = 1.f;
	ClassicFireSound = TEXT("WeaponSFX_SniperRifle.Fire");
	ClassicReloadSound = TEXT("WeaponSFX_SniperRifle.Reload");
	ClassicSelectSound = TEXT("WeaponSFX_SniperRifle.Select");
}

void ARifle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ARifle, ZoomScaleAlpha, COND_OwnerOnly);
}

float ARifle::GetShotPower() const {
	if (!bCharging)
		return ReleasedPower;
	// MF: ShotPower += Delta * 2, from 1 to 10
	return FMath::Clamp(1.f + 2.f * float(GetWorld()->GetTimeSeconds() - ChargeStart), 1.f, 10.f);
}

void ARifle::StartFire(bool bAlt) {
	if (!bAlt && State == EDFWeaponState::Idle && HasAmmoInClip()) {
		bCharging = true;
		ChargeStart = GetWorld()->GetTimeSeconds();
		bFireHeld = true;
		// RageRifle.ClientFire: AmbientSound Powering until ShotPower hits 10 (4.5 s), then PoweredUp
		SetAmbientMFSound(TEXT("WeaponSFX_SniperRifle.Powering"));
		GetWorldTimerManager().SetTimer(PoweredUpTimer, FTimerDelegate::CreateWeakLambda(this, [this]() {
			SetAmbientMFSound(nullptr);
			PlayMFSound(TEXT("WeaponSFX_SniperRifle.PoweredUp"));
		}),
			4.5f, false);
		return; // fires on release
	}
	Super::StartFire(bAlt);
}

void ARifle::StopFire(bool bAlt) {
	if (!bAlt && bCharging) {
		ReleasedPower = GetShotPower();
		bCharging = false;
		bFireHeld = false;
		GetWorldTimerManager().ClearTimer(PoweredUpTimer);
		SetAmbientMFSound(nullptr);
		if (IsLocallyControlled() && GetLocalRole() < ROLE_Authority)
			ServerReleaseFire(ReleasedPower);
		if (IsSimulatingLocally())
			TryFire(false);
		return;
	}
	Super::StopFire(bAlt);
}

void ARifle::ServerReleaseFire_Implementation(float Power) {
	ReleasedPower = FMath::Clamp(Power, 1.f, 10.f);
	bCharging = false;
	TryFire(false);
}

void ARifle::FireShot(bool bAlt) {
	Super::FireShot(bAlt);
	RecoilPower = ReleasedPower * 0.5f; // MF: RecoilPower = ShotPower * 0.5
}

void ARifle::Finish() {
	// Never auto-refire: every shot is charged
	const bool bHeld = bFireHeld;
	bFireHeld = false;
	Super::Finish();
	bFireHeld = bHeld;
}

void ARifle::ProcessTraceHit(const FHitResult& Hit, const FVector& ShotDir, bool bAlt) {
	const float SavedDamage = HitDamage;
	const float SavedMomentum = HitMomentum;
	HitDamage = 20.f * ReleasedPower; // GetShotDamage
	HeadshotDamage = HitDamage * 3.f;
	HitMomentum = 11250.f * ReleasedPower + 11250.f; // GetShotMomentum: 5000 * power + 5000 uu
	Super::ProcessTraceHit(Hit, ShotDir, bAlt);
	HitDamage = SavedDamage;
	HitMomentum = SavedMomentum;
}

float ARifle::GetFOVScale() const {
	if (ZoomLevel == 0)
		return 1.f;
	// MF: ZoomScale = Min + (Max - Min) * alpha^2
	const float Scale = MinZoomScale + (MaxZoomScale - MinZoomScale) * ZoomScaleAlpha * ZoomScaleAlpha;
	return 1.f / Scale;
}

void ARifle::AdjustZoom(int32 Steps) {
	if (ZoomLevel == 0)
		return;
	ZoomScaleAlpha = FMath::Clamp(ZoomScaleAlpha + float(Steps) / float(ZoomScaleIncrements), 0.f, 1.f);
}

FString ARifle::GetHUDStatus() const {
	return bCharging ? FString::Printf(TEXT("POWER %d"), FMath::FloorToInt(GetShotPower())) : FString();
}

#undef LOCTEXT_NAMESPACE
