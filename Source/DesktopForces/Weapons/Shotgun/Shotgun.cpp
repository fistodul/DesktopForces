#include "Weapons/Shotgun/Shotgun.h"
#include "Player/DFSoldierAnimInstance.h"
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
	// MuzzleFlashScale / MuzzleFlashOffset: the flash others see (MachineGunMuzzleFlashMesh as particles)
	ThirdPersonFlashScale = 0.15f;
	ThirdPersonFlashOffset = FVector(8.f, 0.f, 0.f);
	DeathMessage = NSLOCTEXT("DFKills", "Shotgun", "%k blew away %o.");
	ViewMesh = TEXT("RageWeapons/ShotgunPickupCarryMesh");
	ArmsMesh = TEXT("ShotgunPlayerMesh"); // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(1.f, -3.9f, -5.8f);
	ArmsRoll = 0; // shotgun.uc sets no Rotation
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
	// Filip, 2026-11-20: the canvas flash sat off the barrels; at their ends now (DFMuzzleProbe:
	// the middle of both muzzles)
	MuzzleBone = TEXT("Dummy02");
	MuzzleOffset = FVector(-97.58f, -23.31f, 0.65f);
	MuzzleFlashSize = 50.f; // RageWeapons TracerEvery
	FireSide = 1.f;
	AltFireTime = 0.5f; // the faster fan
	ClassicFireSound = TEXT("WeaponSFX_Shotgun.Fire");
	ClassicReloadSound = TEXT("WeaponSFX_Shotgun.Reload");
	ClassicSelectSound = TEXT("WeaponSFX_Shotgun.Select");
	SetIdleFidgets(TEXT("WeaponSFX_Shotgun"));
	IdleFidgets[0] = TEXT("Idle01BothFull"); // shotgun.uc IdleAnims(1-2)
	IdleFidgets[1] = TEXT("Idle02BothFull");
}

void AShotgun::OnLocalReload() {
	// Reloading / ClientReloading.Timer, EjectPause (0.65 s) in: a Shotgun_ShellCase, DrawScale 1.5,
	// from 15 X + (FireOffset.Y + 5) Y - Z; a second when both barrels were spent and more is left
	const bool bBoth = ClipAmmo == 0 && TotalAmmo > 1;
	GetWorldTimerManager().SetTimer(ShellTimer, FTimerDelegate::CreateWeakLambda(this, [this, bBoth]() {
		if (State != EDFWeaponState::Reloading)
			return;
		FDFCasing Shell;
		Shell.Mesh = TEXT("RageWeapons/ShotgunShell");
		Shell.Scale = 1.5f;
		for (int32 i = 0; i < (bBoth ? 2 : 1); ++i)
			EjectCasing(Shell, FVector(15.f, FireSide + 5.f, -1.f));
	}),
		0.65f, false);
}

FName AShotgun::GetArmsAnimName(EDFWeaponState ForState, bool bAlt) {
	UDFTweenAnimInstance* Anim = GetArmsAnim();
	if (!Anim)
		return NAME_None;
	auto First = [Anim](std::initializer_list<const TCHAR*> Names) -> FName {
		for (const TCHAR* N : Names)
			if (Anim->HasSequence(N))
				return N;
		return NAME_None;
	};
	const int32 Shells = ClipAmmo; // after the shot while firing (UseAmmo comes first)
	switch (ForState) {
	case EDFWeaponState::Firing:
		// PlayFiringAnim / PlayAltFiringAnim / PlayTwinFiringAnim
		if (bAlt)
			return First({ TEXT("FireBothBarrelsFull") });
		return Shells >= 1 ? First({ TEXT("FireLeftBarrelRightFull") }) : First({ TEXT("FireRightBarrelLeftEmpty"), TEXT("FireLeftBarrelRightEmpty") });
	case EDFWeaponState::Reloading: // PlayReloadingAnim (the mesh lacks ReloadLeftBarrelRightFull: MF played nothing there)
		return Shells >= 1 ? First({ TEXT("ReloadLeftBarrelRightFull"), TEXT("ReloadBothEmpty") }) : First({ TEXT("ReloadBothEmpty") });
	case EDFWeaponState::Equipping: // PlaySelectAnim (BS_BothFull plays SelectUpBothEmpty, as MF named it)
		return Shells >= 2 ? First({ TEXT("SelectUpBothEmpty") }) : Shells == 1 ? First({ TEXT("SelectUpRightFullLeftEmpty") })
																				: First({ TEXT("SelectUpBothEmptyNoReload"), TEXT("SelectUpBothEmpty") });
	case EDFWeaponState::PuttingDown: // PlayTweenDownAnim
		return Shells >= 2 ? First({ TEXT("SelectDownBothFull") }) : Shells == 1 ? First({ TEXT("SelectDownLeftEmpty") })
																				 : First({ TEXT("SelectDownBothEmpty") });
	case EDFWeaponState::Idle: // PlayIdleAnim: IdleAnims[0] (both empty: StillBothFull, as MF had it)
		return Shells == 1 ? First({ TEXT("StillLeftEmpty"), TEXT("StillBothFull") }) : First({ TEXT("StillBothFull") });
	default:
		return Super::GetArmsAnimName(ForState, bAlt);
	}
}

void AShotgun::UpdateIdleFidgets() {
	// PlayIdleAnim: IdleAnims[1-2] by the barrels
	const TCHAR* Suffix = ClipAmmo >= 2 ? TEXT("BothFull") : ClipAmmo == 1 ? TEXT("LeftEmptyRightFull")
																		   : TEXT("BothEmpty");
	IdleFidgets[0] = *FString::Printf(TEXT("Idle01%s"), Suffix);
	IdleFidgets[1] = *FString::Printf(TEXT("Idle02%s"), Suffix);
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
	if (ShowsFireHere())
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
