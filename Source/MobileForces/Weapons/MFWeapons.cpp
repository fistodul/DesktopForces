#include "Weapons/MFWeapons.h"
#include "Weapons/MFProjectile.h"
#include "Weapons/MFTripBomb.h"
#include "Player/MFCharacterMovement.h"
#include "Kismet/GameplayStatics.h"
#include "Player/MFCharacter.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "MFWeapons"

// ---------------------------------------------------------------------------------------
// Knife
// ---------------------------------------------------------------------------------------

AMFWeapon_Knife::AMFWeapon_Knife()
{
	DisplayName = LOCTEXT("Knife", "Knife");
	InventoryGroup = 1;
	CarrySize = 0;
	bLoadoutSelectable = false;
	bUndroppable = true;
	MaxClipAmmo = 1;
	MaxClips = 1;
	FireTime = 0.55f;          // Slash01-04 at rate 1.5 (0.49-0.61 s)
	ReloadTime = 0.83f;        // ThrowUpCatch at 0.8
	EquipTime = 0.33f;         // SelectUp
	PutDownTime = 0.33f;
	HitDamage = 30.f;
	HitMomentum = 33750.f;     // MF 15000
	DamageType = UMFDamage_Knife::StaticClass();
	AltProjectileClass = AMFProjectile_Knife::StaticClass();
	bInstantHit = true;
	RecoilPower = 0.f;
	MFFireSound = TEXT("WeaponSFX_Knife.Fire01");
	MFSelectSound = TEXT("WeaponSFX_Knife.Select");
}

void AMFWeapon_Knife::FireShot(bool bAlt)
{
	if (!bAlt)
	{
		// Melee: no ammo used
		bLastShotAlt = false;
		if (HasAuthority())
		{
			++BurstCounter;
			const FVector Start = GetFireStart();
			const FVector Dir = GetAimRotation().Vector();
			FCollisionQueryParams Params(SCENE_QUERY_STAT(MFKnife), false, GetOwner());
			FHitResult Hit;
			if (GetWorld()->SweepSingleByChannel(Hit, Start, Start + Dir * MeleeRange, FQuat::Identity, ECC_GameTraceChannel2,
				FCollisionShape::MakeSphere(10.f), Params))
			{
				ProcessTraceHit(Hit, Dir, false);
			}
		}
		if (IsLocallyControlled())
		{
			PlayFireEffects(false);
		}
		return;
	}
	// Throw: the knife leaves the hand until it "returns"
	ClipAmmo = 0;
	if (HasAuthority())
	{
		++BurstCounter;
		SpawnProjectile(AltProjectileClass, 0.f, true);
		GetWorldTimerManager().SetTimer(ReturnTimer, this, &AMFWeapon_Knife::KnifeReturned, KnifeReturnTime, false);
	}
}

void AMFWeapon_Knife::Finish()
{
	// No reloading: an empty knife just idles until it comes back
	State = EMFWeaponState::Idle;
	if (bFireHeld && HasAmmoInClip())
	{
		TryFire(false);
	}
}

void AMFWeapon_Knife::KnifeReturned()
{
	ClipAmmo = 1;
}

// ---------------------------------------------------------------------------------------
// Pistol
// ---------------------------------------------------------------------------------------

AMFWeapon_Pistol::AMFWeapon_Pistol()
{
	DisplayName = LOCTEXT("Pistol", "Pistol");
	InventoryGroup = 2;
	CarrySize = 1;
	AIRating = 0.45f;
	MaxClipAmmo = 8;
	MaxClips = 6;
	FireTime = 0.342f;         // Fire 15 frames @48 + 0.05 tween
	ZoomedFireTime = 0.5f;     // AccurateAimFire 16 @30
	ReloadTime = 1.55f;        // Reload 40 @30 + 0.25 tween
	EquipTime = 0.667f;        // SelectUp 21 @30
	PutDownTime = 0.7f;        // SelectDown 22 @30
	ZoomTime = 0.333f;         // AccurateAimUp 11 @30
	HitDamage = 26.f;
	HeadshotDamage = 40.f;
	Accuracy = 0.4f;
	AltAccuracy = 0.05f;
	MaxZoomLevel = 1;
	ZoomScale = 1.5f;
	RecoilPower = 1.f;
	MFFireSound = TEXT("WeaponSFX_Pistol.Fire");
	MFReloadSound = TEXT("WeaponSFX_Pistol.Reload");
	MFSelectSound = TEXT("WeaponSFX_Pistol.Select");
}

// ---------------------------------------------------------------------------------------
// Machine gun
// ---------------------------------------------------------------------------------------

AMFWeapon_MachineGun::AMFWeapon_MachineGun()
{
	DisplayName = LOCTEXT("MachineGun", "Machine Gun");
	InventoryGroup = 3;
	CarrySize = 2;
	AIRating = 0.7f;
	MaxClipAmmo = 48;
	MaxClips = 5;
	FireTime = 0.117f;         // ContinuousFire 3 frames @30 + 0.05 tween
	ZoomedFireTime = 0.333f;   // AccurateAimFire 11 @30
	ReloadTime = 2.25f;        // Reload 41 @20 + 0.25
	EquipTime = 0.333f;
	PutDownTime = 0.333f;
	ZoomTime = 0.333f;
	HitDamage = 20.f;
	Accuracy = 0.7f;
	AltAccuracy = 0.1f;
	MaxZoomLevel = 1;
	ZoomScale = 2.f;
	RecoilPower = 0.8f;
	MFFireSound = TEXT("WeaponSFX_MachineGun.Fire");
	MFReloadSound = TEXT("WeaponSFX_MachineGun.Reload");
	MFSelectSound = TEXT("WeaponSFX_MachineGun.Select");
}

// ---------------------------------------------------------------------------------------
// Shotgun
// ---------------------------------------------------------------------------------------

AMFWeapon_Shotgun::AMFWeapon_Shotgun()
{
	DisplayName = LOCTEXT("Shotgun", "Shotgun");
	InventoryGroup = 4;
	CarrySize = 2;
	AIRating = 0.6f;
	MaxClipAmmo = 2;
	MaxClips = 20;
	FireTime = 0.472f;         // Fire*Barrel 20 frames @30 x1.5 + 0.05
	ReloadTime = 0.867f;       // Reload* 40 @30 x1.5
	EquipTime = 0.717f;
	PutDownTime = 0.383f;
	HitDamage = 12.f;
	Pellets = 10;              // NumShellFragments
	Accuracy = 0.8f;
	AltAccuracy = 1.f;
	MaxTraceDistance = 11250.f; // MF 5000
	DamageType = UMFDamage_Shotgun::StaticClass();
	RecoilPower = 0.42f;
	MFFireSound = TEXT("WeaponSFX_Shotgun.Fire");
	MFReloadSound = TEXT("WeaponSFX_Shotgun.Reload");
	MFSelectSound = TEXT("WeaponSFX_Shotgun.Select");
}

void AMFWeapon_Shotgun::StartFire(bool bAlt)
{
	// The other button pressed during the window turns the shot into both barrels
	if (GetWorldTimerManager().IsTimerActive(WindowTimer) && bAlt != bWindowAlt)
	{
		bTwin = true;
		(bAlt ? bAltFireHeld : bFireHeld) = true;
		if (GetLocalRole() < ROLE_Authority)
		{
			ServerStartFire(bAlt);
		}
		return;
	}
	Super::StartFire(bAlt);
}

bool AMFWeapon_Shotgun::TryFire(bool bAlt)
{
	if (!HasAmmoInClip())
	{
		return Super::TryFire(bAlt);
	}
	bWindowAlt = bAlt;
	bTwin = false;
	State = EMFWeaponState::Firing;
	GetWorldTimerManager().SetTimer(WindowTimer, this, &AMFWeapon_Shotgun::FireAfterWindow, TwinFireWindow, false);
	return true;
}

void AMFWeapon_Shotgun::FireAfterWindow()
{
	if (bTwin && ClipAmmo < 2)
	{
		bTwin = false;
	}
	SetState(EMFWeaponState::Firing, FireTime);
	FireShot(bWindowAlt);
}

void AMFWeapon_Shotgun::FireShot(bool bAlt)
{
	const int32 Barrels = bTwin ? 2 : 1;
	ClipAmmo = FMath::Max(0, ClipAmmo - Barrels);
	bLastShotAlt = bAlt;
	if (HasAuthority())
	{
		++BurstCounter;
		TraceFire(bAlt ? AltAccuracy : Accuracy, Pellets * Barrels, bAlt);
	}
	if (IsLocallyControlled() || GetNetMode() == NM_Standalone)
	{
		PlayFireEffects(bAlt);
	}
	bTwin = false;
}

// ---------------------------------------------------------------------------------------
// Sniper rifle
// ---------------------------------------------------------------------------------------

AMFWeapon_Rifle::AMFWeapon_Rifle()
{
	DisplayName = LOCTEXT("Rifle", "Sniper Rifle");
	InventoryGroup = 5;
	CarrySize = 3;
	AIRating = 0.5f;
	MaxClipAmmo = 4;
	MaxClips = 4;
	FireTime = 0.55f;          // Fire 16 @30 + 0.05
	ZoomedFireTime = 0.55f;
	ReloadTime = 1.383f;       // Reload 41 @30 + 0.05
	EquipTime = 0.667f;        // SelectUp at rate 0.5
	PutDownTime = 0.667f;
	ZoomTime = 0.667f;         // BringScopeUp at rate 0.5
	HitDamage = 20.f;          // x ShotPower
	Accuracy = 0.3f;
	AltAccuracy = 0.f;
	MaxTraceDistance = 45000.f; // MF 20000
	MaxZoomLevel = 1;
	ZoomScale = 3.f;
	ZoomedMoveScale = 0.f;     // can't walk while scoped (MF: "no use to players on the move")
	RecoilPower = 1.f;
	MFFireSound = TEXT("WeaponSFX_SniperRifle.Fire");
	MFReloadSound = TEXT("WeaponSFX_SniperRifle.Reload");
	MFSelectSound = TEXT("WeaponSFX_SniperRifle.Select");
}

void AMFWeapon_Rifle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AMFWeapon_Rifle, ZoomScaleAlpha, COND_OwnerOnly);
}

float AMFWeapon_Rifle::GetShotPower() const
{
	if (!bCharging)
	{
		return ReleasedPower;
	}
	// MF: ShotPower += Delta * 2, from 1 to 10
	return FMath::Clamp(1.f + 2.f * float(GetWorld()->GetTimeSeconds() - ChargeStart), 1.f, 10.f);
}

void AMFWeapon_Rifle::StartFire(bool bAlt)
{
	if (!bAlt && State == EMFWeaponState::Idle && HasAmmoInClip())
	{
		bCharging = true;
		ChargeStart = GetWorld()->GetTimeSeconds();
		bFireHeld = true;
		return;   // fires on release
	}
	Super::StartFire(bAlt);
}

void AMFWeapon_Rifle::StopFire(bool bAlt)
{
	if (!bAlt && bCharging)
	{
		ReleasedPower = GetShotPower();
		bCharging = false;
		bFireHeld = false;
		if (IsLocallyControlled() && GetLocalRole() < ROLE_Authority)
		{
			ServerReleaseFire(ReleasedPower);
		}
		if (IsSimulatingLocally())
		{
			TryFire(false);
		}
		return;
	}
	Super::StopFire(bAlt);
}

void AMFWeapon_Rifle::ServerReleaseFire_Implementation(float Power)
{
	ReleasedPower = FMath::Clamp(Power, 1.f, 10.f);
	bCharging = false;
	TryFire(false);
}

void AMFWeapon_Rifle::FireShot(bool bAlt)
{
	Super::FireShot(bAlt);
	RecoilPower = ReleasedPower * 0.5f;   // MF: RecoilPower = ShotPower * 0.5
}

void AMFWeapon_Rifle::Finish()
{
	// Never auto-refire: every shot is charged
	const bool bHeld = bFireHeld;
	bFireHeld = false;
	Super::Finish();
	bFireHeld = bHeld;
}

void AMFWeapon_Rifle::ProcessTraceHit(const FHitResult& Hit, const FVector& ShotDir, bool bAlt)
{
	const float SavedDamage = HitDamage;
	const float SavedMomentum = HitMomentum;
	HitDamage = 20.f * ReleasedPower;                       // GetShotDamage
	HeadshotDamage = HitDamage * 3.f;
	HitMomentum = 11250.f * ReleasedPower + 11250.f;        // GetShotMomentum: 5000 * power + 5000 uu
	Super::ProcessTraceHit(Hit, ShotDir, bAlt);
	HitDamage = SavedDamage;
	HitMomentum = SavedMomentum;
}

float AMFWeapon_Rifle::GetFOVScale() const
{
	if (ZoomLevel == 0)
	{
		return 1.f;
	}
	// MF: ZoomScale = Min + (Max - Min) * alpha^2
	const float Scale = MinZoomScale + (MaxZoomScale - MinZoomScale) * ZoomScaleAlpha * ZoomScaleAlpha;
	return 1.f / Scale;
}

void AMFWeapon_Rifle::AdjustZoom(int32 Steps)
{
	if (ZoomLevel == 0)
	{
		return;
	}
	ZoomScaleAlpha = FMath::Clamp(ZoomScaleAlpha + float(Steps) / float(ZoomScaleIncrements), 0.f, 1.f);
}

FString AMFWeapon_Rifle::GetHUDStatus() const
{
	return bCharging ? FString::Printf(TEXT("POWER %d"), FMath::FloorToInt(GetShotPower())) : FString();
}

// ---------------------------------------------------------------------------------------
// Rocket launcher
// ---------------------------------------------------------------------------------------

AMFWeapon_RocketLauncher::AMFWeapon_RocketLauncher()
{
	DisplayName = LOCTEXT("RocketLauncher", "Rocket Launcher");
	InventoryGroup = 7;
	CarrySize = 4;
	AIRating = 0.6f;
	MaxClipAmmo = 1;
	MaxClips = 11;
	FireTime = 0.79f;          // Fire 21 @30 x0.9 + 0.05
	ZoomedFireTime = 0.79f;
	ReloadTime = 3.38f;        // Reload 41 @30 x0.4 + 0.05
	EquipTime = 0.667f;
	PutDownTime = 0.526f;      // SelectDown 21 @38
	ZoomTime = 1.333f;         // BringScopeUp 21 @30 x0.5
	bInstantHit = false;
	ProjectileClass = AMFProjectile_Rocket::StaticClass();
	AltProjectileClass = AMFProjectile_Rocket::StaticClass();
	MaxZoomLevel = 1;
	ZoomScale = 4.f;
	RecoilPower = 8.f;
	MFFireSound = TEXT("WeaponSFX_RocketLauncher.Fire");
	MFReloadSound = TEXT("WeaponSFX_RocketLauncher.Reload");
	MFSelectSound = TEXT("WeaponSFX_RocketLauncher.Select");
}

// ---------------------------------------------------------------------------------------
// Grenades
// ---------------------------------------------------------------------------------------

AMFWeapon_Grenades::AMFWeapon_Grenades()
{
	DisplayName = LOCTEXT("Grenades", "Grenades");
	InventoryGroup = 8;
	CarrySize = 1;
	MaxCanCarry = 3;
	AIRating = 0.4f;
	MaxClipAmmo = 3;
	MaxClips = 1;
	bDestroyWhenEmpty = true;
	FireTime = 0.333f;         // ThrowOver 11 @30
	EquipTime = 0.5f;          // SelectUp 16 @30
	PutDownTime = 0.5f;
	bInstantHit = false;
	ProjectileClass = AMFProjectile_Grenade::StaticClass();
	AltProjectileClass = AMFProjectile_ImpactGrenade::StaticClass();
	RecoilPower = 0.f;
	MFFireSound = TEXT("WeaponSFX_Grenades.Fire");
	MFSelectSound = TEXT("WeaponSFX_Grenades.Select");
}

float AMFWeapon_Grenades::GetThrowPower() const
{
	if (!bWinding)
	{
		return 0.f;
	}
	// MF: ThrowPower += Delta * 8, max 10, min 1.5 on release
	return FMath::Clamp(8.f * float(GetWorld()->GetTimeSeconds() - WindUpStart), 1.5f, 10.f);
}

void AMFWeapon_Grenades::StartFire(bool bAlt)
{
	if (State != EMFWeaponState::Idle || !HasAmmoInClip() || bWinding)
	{
		return;
	}
	bWinding = true;
	bWindingAlt = bAlt;
	WindUpStart = GetWorld()->GetTimeSeconds();
}

void AMFWeapon_Grenades::StopFire(bool bAlt)
{
	if (!bWinding || bAlt != bWindingAlt)
	{
		return;
	}
	const float Power = GetThrowPower();
	bWinding = false;
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerThrow(bAlt, Power);
	}
	Throw(bAlt, Power);
}

void AMFWeapon_Grenades::ServerThrow_Implementation(bool bAlt, float Power)
{
	Throw(bAlt, FMath::Clamp(Power, 1.5f, 10.f));
}

void AMFWeapon_Grenades::Throw(bool bAlt, float Power)
{
	if (!IsSimulatingLocally() || !HasAmmoInClip())
	{
		return;
	}
	ClipAmmo -= 1;
	SetState(EMFWeaponState::Firing, FireTime);
	if (HasAuthority())
	{
		++BurstCounter;
		SpawnProjectile(bAlt ? AltProjectileClass : ProjectileClass, Power * 337.5f, bAlt); // MF ThrowPower * 150 uu/s
	}
	if (IsLocallyControlled())
	{
		PlayFireEffects(bAlt);
	}
}

FString AMFWeapon_Grenades::GetHUDStatus() const
{
	return bWinding ? FString::Printf(TEXT("THROW %d"), FMath::FloorToInt(GetThrowPower())) : FString();
}

// ---------------------------------------------------------------------------------------
// Heavy machine gun
// ---------------------------------------------------------------------------------------

AMFWeapon_HeavyMachineGun::AMFWeapon_HeavyMachineGun()
{
	DisplayName = LOCTEXT("HMG", "Heavy Machine Gun");
	InventoryGroup = 6;
	CarrySize = 4;
	AIRating = 0.9f;
	MaxClipAmmo = 900;
	MaxClips = 1;
	EquipTime = 0.667f;         // SelectUp 21 @30
	PutDownTime = 0.667f;
	ZoomTime = 1.026f;          // deploy: ActivateGun01 41 frames @30 x1.3
	HitDamage = 45.f;
	Accuracy = 0.6f;            // spread at standstill ...
	AltAccuracy = 1.2f;         // ... and at full spin
	MaxTraceDistance = 11250.f; // MF 5000
	MaxZoomLevel = 1;           // MF models "deployed" as zoom level 1
	ZoomScale = 1.f;
	ZoomedMoveScale = 0.f;      // can't move while deployed
	RecoilPower = 0.3f;
	MFFireSound = TEXT("WeaponSFX_HeavyMachineGun.Fire");
	MFSelectSound = TEXT("WeaponSFX_HeavyMachineGun.Select");
}

void AMFWeapon_HeavyMachineGun::ToggleZoom()
{
	// MF OKToDeploy: only standing on the ground (not in water, not falling)
	const AMFCharacter* C = GetOwnerCharacter();
	if (ZoomLevel == 0 && (!C || !C->GetCharacterMovement()->IsMovingOnGround()))
	{
		return;
	}
	if (ZoomLevel > 0)
	{
		GetWorldTimerManager().ClearTimer(SpinTimer);
		SpinRate = 0.f;
	}
	Super::ToggleZoom();
}

void AMFWeapon_HeavyMachineGun::StartFire(bool bAlt)
{
	if (bAlt)
	{
		Super::StartFire(true);   // deploy / pick up
		return;
	}
	bFireHeld = true;
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerStartFire(false);
	}
	if (IsSimulatingLocally() && ZoomLevel > 0 && State == EMFWeaponState::Idle && HasAmmoInClip())
	{
		State = EMFWeaponState::Firing;
		GetWorldTimerManager().SetTimer(SpinTimer, this, &AMFWeapon_HeavyMachineGun::SpinTick, 1.f / 30.f, true);
	}
}

void AMFWeapon_HeavyMachineGun::StopFire(bool bAlt)
{
	Super::StopFire(bAlt);   // clears the held flag; SpinTick winds down
}

void AMFWeapon_HeavyMachineGun::SpinTick()
{
	const float Dt = 1.f / 30.f;
	if (bFireHeld && ZoomLevel > 0 && HasAmmoInClip())
	{
		SpinRate = FMath::Min(MaxSpin, SpinRate + SpinAccel * Dt);
		ShotAccumulator += SpinRate * RoundsPerCycle * Dt;
		while (ShotAccumulator >= 1.f && HasAmmoInClip())
		{
			ShotAccumulator -= 1.f;
			ClipAmmo -= 1;
			if (HasAuthority())
			{
				++BurstCounter;
				TraceFire(FMath::Lerp(Accuracy, AltAccuracy, SpinRate / MaxSpin), 1, false);
			}
			if (IsLocallyControlled() || GetNetMode() == NM_Standalone)
			{
				PlayFireEffects(false);
			}
		}
		return;
	}
	// Spin down (MF ZoomedIn.Tick), then stop
	SpinRate -= SpinAccel * Dt;
	if (SpinRate <= MinSpin)
	{
		SpinRate = 0.f;
		ShotAccumulator = 0.f;
		GetWorldTimerManager().ClearTimer(SpinTimer);
		State = EMFWeaponState::Idle;
		if (!HasAmmoInClip() && HasAuthority())
		{
			if (AMFCharacter* C = GetOwnerCharacter())
			{
				C->SwitchToBestWeapon();
			}
		}
	}
}

void AMFWeapon_HeavyMachineGun::ForceInactive()
{
	GetWorldTimerManager().ClearTimer(SpinTimer);
	SpinRate = 0.f;
	Super::ForceInactive();
}

FString AMFWeapon_HeavyMachineGun::GetHUDStatus() const
{
	return ZoomLevel > 0 ? FString() : TEXT("Alt-fire to deploy");
}

// ---------------------------------------------------------------------------------------
// Trip bombs
// ---------------------------------------------------------------------------------------

AMFWeapon_TripBombs::AMFWeapon_TripBombs()
{
	DisplayName = LOCTEXT("TripBombs", "Trip Bombs");
	InventoryGroup = 9;
	CarrySize = 1;
	MaxCanCarry = 3;
	AIRating = 0.4f;
	MaxClipAmmo = 2;
	MaxClips = 1;
	bDestroyWhenEmpty = true;
	FireTime = 0.967f;          // PlaceForward 29 @30
	EquipTime = 0.6f;           // SelectUp 19 @30
	PutDownTime = 0.667f;
	bInstantHit = false;
	AltProjectileClass = AMFProjectile_TripMine::StaticClass();
	RecoilPower = 0.f;
	MFFireSound = TEXT("WeaponSFX_TripBombs.Plant");
	MFSelectSound = TEXT("WeaponSFX_TripBombs.Idle01");
}

float AMFWeapon_TripBombs::GetThrowPower() const
{
	// MF: ThrowPower += Delta * 5 (max 10); +3 if released under 3
	float P = FMath::Min(10.f, 5.f * float(GetWorld()->GetTimeSeconds() - WindUpStart));
	return P < 3.f ? P + 3.f : P;
}

void AMFWeapon_TripBombs::StartFire(bool bAlt)
{
	if (State != EMFWeaponState::Idle || !HasAmmoInClip())
	{
		return;
	}
	if (bAlt)
	{
		bWinding = true;
		WindUpStart = GetWorld()->GetTimeSeconds();
		return;
	}
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerUse(false, 0.f);
	}
	Use(false, 0.f);
}

void AMFWeapon_TripBombs::StopFire(bool bAlt)
{
	if (!bAlt || !bWinding)
	{
		return;
	}
	bWinding = false;
	const float Power = GetThrowPower();
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerUse(true, Power);
	}
	Use(true, Power);
}

void AMFWeapon_TripBombs::ServerUse_Implementation(bool bAlt, float Power)
{
	Use(bAlt, FMath::Clamp(Power, 3.f, 10.f));
}

void AMFWeapon_TripBombs::Use(bool bAlt, float Power)
{
	if (!IsSimulatingLocally() || !HasAmmoInClip() || State != EMFWeaponState::Idle)
	{
		return;
	}
	if (!bAlt)
	{
		if (HasAuthority() && !TryPlant())
		{
			return;
		}
		if (!HasAuthority())
		{
			// predict only the sound; the server decides whether the plant worked
			PlaySoundLocal(FireSound);
			return;
		}
	}
	ClipAmmo -= 1;
	SetState(EMFWeaponState::Firing, FireTime);
	if (HasAuthority())
	{
		++BurstCounter;
		if (bAlt)
		{
			SpawnProjectile(AltProjectileClass, Power * 281.25f, true);   // MF ThrowPower * 125 uu/s
		}
	}
	if (IsLocallyControlled())
	{
		PlayFireEffects(bAlt);
	}
}

bool AMFWeapon_TripBombs::TryPlant()
{
	AMFCharacter* C = GetOwnerCharacter();
	if (!C)
	{
		return false;
	}
	// MF: trace 50 uu ahead; the laser must hit something within MaxLaserDistance
	const FVector Start = GetFireStart();
	const FVector Dir = GetAimRotation().Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFPlant), false, C);
	FHitResult Wall, Opposite;
	if (!GetWorld()->LineTraceSingleByChannel(Wall, Start, Start + Dir * PlaceReach, ECC_Visibility, Params))
	{
		return false;
	}
	const FVector BombLoc = Wall.ImpactPoint + Wall.ImpactNormal * 6.f;
	if (!GetWorld()->LineTraceSingleByChannel(Opposite, BombLoc, BombLoc + Wall.ImpactNormal * MaxLaserDistance, ECC_Visibility, Params))
	{
		UGameplayStatics::PlaySoundAtLocation(this, LoadObject<USoundBase>(nullptr, TEXT("/Game/MFImported/Sounds/WeaponSFX_TripBombs/PlantFail.PlantFail"), nullptr, LOAD_NoWarn | LOAD_Quiet), BombLoc);
		return false;   // too far for the laser to work reliably
	}
	FActorSpawnParameters SP;
	SP.Owner = C;
	SP.Instigator = C;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AMFTripBomb* Bomb = GetWorld()->SpawnActor<AMFTripBomb>(BombLoc, Wall.ImpactNormal.Rotation(), SP))
	{
		Bomb->MaxLaserDistance = MaxLaserDistance;
		Bomb->Team = C->GetTeam();
		return true;
	}
	return false;
}

FString AMFWeapon_TripBombs::GetHUDStatus() const
{
	return bWinding ? FString::Printf(TEXT("THROW %d"), FMath::FloorToInt(GetThrowPower())) : FString();
}

// ---------------------------------------------------------------------------------------
// Adrenaline shot
// ---------------------------------------------------------------------------------------

AMFWeapon_AdrenalineShot::AMFWeapon_AdrenalineShot()
{
	DisplayName = LOCTEXT("Adrenaline", "Adrenaline Shot");
	InventoryGroup = 10;
	CarrySize = 1;
	AutoSwitchPriority = 0.f;
	MaxClipAmmo = 1;
	MaxClips = 1;
	bDestroyWhenEmpty = true;
	FireTime = 0.5f;
	EquipTime = 0.2f;
	PutDownTime = 0.2f;
	RecoilPower = 0.f;
}

bool AMFWeapon_AdrenalineShot::TryFire(bool bAlt)
{
	AMFCharacter* Me = GetOwnerCharacter();
	if (!Me || !HasAmmoInClip())
	{
		return false;
	}
	AMFCharacter* Target = Me;
	if (bAlt)
	{
		// MF TryToInject: trace Range along the view
		Target = nullptr;
		FHitResult Hit;
		const FVector Start = GetFireStart();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MFInject), false, Me);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + GetAimRotation().Vector() * InjectRange, ECC_GameTraceChannel2, Params))
		{
			Target = Cast<AMFCharacter>(Hit.GetActor());
		}
	}
	// MF InjectDrug: only used when the target is hurt
	if (!Target || Target->GetHealth() >= Target->MaxHealth)
	{
		return false;
	}
	SetState(EMFWeaponState::Firing, FireTime);
	ClipAmmo -= 1;
	if (HasAuthority())
	{
		Target->Heal(Target->MaxHealth);
	}
	return true;
}

// ---------------------------------------------------------------------------------------
// Armour
// ---------------------------------------------------------------------------------------

AMFWeapon_Armour::AMFWeapon_Armour()
{
	DisplayName = LOCTEXT("Armour", "Body Armour");
	InventoryGroup = 11;
	CarrySize = 1;
	bSelectable = false;
	AutoSwitchPriority = 0.f;
	AIRating = -1.f;
	MaxClipAmmo = 1;
	MaxClips = 1;
}

void AMFWeapon_Armour::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFWeapon_Armour, Charge);
}

void AMFWeapon_Armour::GiveFullAmmo()
{
	Super::GiveFullAmmo();
	Charge = MaxCharge;
}

int32 AMFWeapon_Armour::AbsorbDamage(int32 Damage, const UMFDamageType* Type)
{
	// Inventory.ArmorAbsorbDamage + RageArmour: drowning and falling bypass armour
	if (Type && Type->bBypassArmour)
	{
		return Damage;
	}
	int32 ArmorDamage = Damage * ArmorAbsorption / 100;
	if (ArmorDamage >= Charge)
	{
		ArmorDamage = Charge;
		Charge = 0;
		if (AMFCharacter* C = GetOwnerCharacter())
		{
			C->RemoveWeapon(this, true);   // worn out
		}
	}
	else
	{
		Charge -= ArmorDamage;
	}
	return Damage - ArmorDamage;
}

#undef LOCTEXT_NAMESPACE
