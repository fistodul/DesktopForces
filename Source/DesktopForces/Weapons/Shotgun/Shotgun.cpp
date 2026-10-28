#include "Weapons/Shotgun/Shotgun.h"
#include "Game/DFGameState.h"
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

AShotgun::AShotgun() {
	ViewMesh = TEXT("RageWeapons/ShotgunPickupCarryMesh");
	ArmsMesh = TEXT("ShotgunPlayerMesh"); // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(1.f, -3.9f, -5.8f);
	HUDIconRect = FIntRect(160, 128, 256, 192); // WeaponIcon X=160 Y=128 W=96 H=64
	DisplayName = LOCTEXT("Shotgun", "Shotgun");
	InventoryGroup = 4;
	CarrySize = 2;
	AIRating = 0.6f;
	MaxClipAmmo = 2;
	MaxClips = 20;
	FireTime = 0.472f; // Fire*Barrel 20 frames @30 x1.5 + 0.05
	ReloadTime = 0.867f; // Reload* 40 @30 x1.5
	EquipTime = 0.717f;
	PutDownTime = 0.383f;
	HitDamage = 12.f;
	Pellets = 10; // NumShellFragments
	Accuracy = 0.8f;
	AltAccuracy = 1.f;
	MaxTraceDistance = 11250.f; // MF 5000
	DamageType = UDFDamage_Shotgun::StaticClass();
	RecoilPower = 0.42f;
	TracerEvery = 2;
	MuzzleFlashSize = 50.f; // RageWeapons TracerEvery
	ClassicFireSound = TEXT("WeaponSFX_Shotgun.Fire");
	ClassicReloadSound = TEXT("WeaponSFX_Shotgun.Reload");
	ClassicSelectSound = TEXT("WeaponSFX_Shotgun.Select");
}

void AShotgun::BeginPlay() {
	Super::BeginPlay();
	if (const ADFGameState* GS = GetWorld()->GetGameState<ADFGameState>(); GS && GS->bBoomstick)
		ApplyBoomstick();
}

void AShotgun::ApplyBoomstick() {
	DisplayName = LOCTEXT("Boomstick", "Boomstick");
	Pellets = 12; // ZombieShotgun NumShellFragments
	HitDamage = 15.f; // ProcessTraceHit: 15 vs 12
	MaxClips = 22;
	RecoilPower = 0.15f;
}

bool AShotgun::TryFire(bool bAlt) {
	if (!HasAmmoInClip())
		return Super::TryFire(bAlt);
	// alt: both barrels if two shells are loaded (FireShot's "alt" is "both" from here on)
	const bool bBoth = bAlt && (ClipAmmo >= 2 || HasInfiniteAmmo());
	SetState(EDFWeaponState::Firing, FireTime);
	FireShot(bBoth);
	return true;
}

void AShotgun::FireShot(bool bBoth) {
	const int32 Barrels = bBoth ? 2 : 1;
	UseAmmo(Barrels);
	bLastShotAlt = bBoth;
	if (HasAuthority()) {
		++BurstCounter;
		TraceFire(bBoth ? AltAccuracy : Accuracy, Pellets * Barrels, bBoth);
	}
	if (IsLocallyControlled() || GetNetMode() == NM_Standalone)
		PlayFireEffects(bBoth);
}

void AShotgun::PlayFireEffects(bool bBoth) {
	if (!bBoth) {
		Super::PlayFireEffects(false);
		return;
	}
	// both barrels: TwinFireSound
	const TObjectPtr<USoundBase> Single = FireSound;
	FireSound = nullptr;
	Super::PlayFireEffects(true);
	FireSound = Single;
	PlayMFSound(TEXT("WeaponSFX_Shotgun.FireBoth"));
}

#undef LOCTEXT_NAMESPACE
