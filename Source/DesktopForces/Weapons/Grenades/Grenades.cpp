#include "Weapons/Grenades/Grenades.h"
#include "Player/DFSoldierAnimInstance.h"
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

AGrenades::AGrenades() {
	ViewMesh = TEXT("RageWeapons/GrenadesPickupCarryMesh");
	ArmsMesh = TEXT("GrenadesPlayerMesh"); // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(1.6f, -1.f, -4.f);
	HUDIconRect = FIntRect(48, 0, 96, 64); // WeaponIcon X=48 Y=0 W=48 H=64
	DisplayName = LOCTEXT("Grenades", "Grenades");
	InventoryGroup = 8;
	CarrySize = 1;
	MaxCanCarry = 3;
	AIRating = 0.4f;
	MaxClipAmmo = 3;
	MaxClips = 1;
	bDestroyWhenEmpty = true;
	FireTime = 0.333f; // ThrowOver 11 @30
	EquipTime = 0.5f; // SelectUp 16 @30
	PutDownTime = 0.5f;
	bInstantHit = false;
	ProjectileClass = AGrenade::StaticClass();
	AltProjectileClass = AImpactGrenade::StaticClass();
	RecoilPower = 0.f;
	ClassicFireSound = TEXT("WeaponSFX_Grenades.Fire");
	ClassicSelectSound = TEXT("WeaponSFX_Grenades.Select");
}

float AGrenades::GetThrowPower() const {
	if (!bWinding)
		return 0.f;
	// MF: ThrowPower += Delta * 8, max 10, min 1.5 on release
	return FMath::Clamp(8.f * float(GetWorld()->GetTimeSeconds() - WindUpStart), 1.5f, 10.f);
}

void AGrenades::StartFire(bool bAlt) {
	if (State != EDFWeaponState::Idle || !HasAmmoInClip() || bWinding)
		return;
	bWinding = true;
	bWindingAlt = bAlt;
	WindUpStart = GetWorld()->GetTimeSeconds();
	// PlayFiringAnim: the pin comes out with the teeth or by hand, then Powering while winding up
	const bool bTeeth = FMath::RandBool();
	PlayMFSound(bTeeth ? TEXT("WeaponSFX_Grenades.PullPinTeeth") : TEXT("WeaponSFX_Grenades.PullPinHand"));
	SetAmbientMFSound(TEXT("WeaponSFX_Grenades.Powering"));
	if (UDFTweenAnimInstance* Anim = IsLocallyControlled() ? GetArmsAnim() : nullptr) {
		Anim->SetIdle(NAME_None); // the arm stays drawn back until the throw
		Anim->Play(bTeeth ? TEXT("PullPinBack02") : TEXT("PullPinBack"), 0.9f, 0.05f, false);
	}
}

void AGrenades::StopFire(bool bAlt) {
	if (!bWinding || bAlt != bWindingAlt)
		return;
	const float Power = GetThrowPower();
	bWinding = false;
	SetAmbientMFSound(nullptr);
	if (GetLocalRole() < ROLE_Authority)
		ServerThrow(bAlt, Power);
	Throw(bAlt, Power);
}

void AGrenades::ServerThrow_Implementation(bool bAlt, float Power) {
	Throw(bAlt, FMath::Clamp(Power, 1.5f, 10.f));
}

void AGrenades::Throw(bool bAlt, float Power) {
	if (!IsSimulatingLocally() || !HasAmmoInClip())
		return;
	UseAmmo();
	SetState(EDFWeaponState::Firing, FireTime);
	if (HasAuthority()) {
		++BurstCounter;
		SpawnProjectile(bAlt ? AltProjectileClass : ProjectileClass, Power * 337.5f, bAlt); // MF ThrowPower * 150 uu/s
	}
	if (IsLocallyControlled())
		PlayFireEffects(bAlt);
}

FString AGrenades::GetHUDStatus() const {
	return bWinding ? FString::Printf(TEXT("THROW %d"), FMath::FloorToInt(GetThrowPower())) : FString();
}

#undef LOCTEXT_NAMESPACE
