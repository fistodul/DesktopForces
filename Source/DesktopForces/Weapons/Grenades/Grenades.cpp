#include "Weapons/Grenades/Grenades.h"
#include "Player/DFSoldierAnimInstance.h"
#include "Weapons/DFProjectile.h"
#include "Weapons/DFShellCase.h"
#include "Core/DFUserSettings.h"
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
	DeathMessage = NSLOCTEXT("DFKills", "Grenades", "%k Blew Up %o.");
	ViewMesh = TEXT("RageWeapons/GrenadesPickupCarryMesh");
	ArmsMesh = TEXT("GrenadesPlayerMesh"); // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ThrowReleaseMin = 1.5f; // ThrowPower += Delta * 8 (the default rate), at least 1.5 on release
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
	ClassicSelectSound = TEXT("WeaponSFX_HeavyMachineGun.Select"); // Grenades' SelectSound (RageWeapon's)
	SetIdleFidgets(nullptr); // IdleAnims(1-2), no IdleSound
}

void AGrenades::StartFire(bool bAlt) {
	if (State != EDFWeaponState::Idle || !HasAmmoInClip() || bWinding)
		return;
	bWindingAlt = bAlt;
	bReleased = false;
	GetWorldTimerManager().ClearTimer(NextTimer);
	// PlayFiringAnim: the pin comes out with the teeth or by hand (PullPinBack(02) at 0.9), then
	// Powering while winding up. The power charges and a release throws only once the arm is back
	// (MF Begin: FinishAnim, then Tick): a tap throws at the end of the pull
	const bool bTeeth = FMath::RandBool();
	const FName Pull = bTeeth ? TEXT("PullPinBack02") : TEXT("PullPinBack");
	UDFTweenAnimInstance* Anim = IsLocallyControlled() ? GetArmsAnim() : nullptr;
	const float PullTime = Anim ? Anim->GetLength(Pull) / 0.9f : 30.f / 30.f / 0.9f; // 30 frames @30
	StartWindUp(PullTime);
	PlayMFSound(bTeeth ? TEXT("WeaponSFX_Grenades.PullPinTeeth") : TEXT("WeaponSFX_Grenades.PullPinHand"));
	SetAmbientMFSound(TEXT("WeaponSFX_Grenades.Powering"));
	if (Anim) {
		Anim->SetIdle(NAME_None); // the arm stays drawn back until the throw
		Anim->Play(Pull, 0.9f, 0.05f, false);
		// EjectPause 0.65 (hand), EjectPause2 0.85 (teeth)
		GetWorldTimerManager().SetTimer(EjectTimer, FTimerDelegate::CreateWeakLambda(this, [this, bTeeth]() { EjectPin(bTeeth); }), bTeeth ? 0.85f : 0.65f, false);
	}
	GetWorldTimerManager().SetTimer(PinTimer, FTimerDelegate::CreateWeakLambda(this, [this]() {
		if (bReleased)
			Release();
	}),
		FMath::Max(PullTime, 0.01f), false);
}

void AGrenades::StopFire(bool bAlt) {
	if (!bWinding || bAlt != bWindingAlt)
		return;
	bReleased = true;
	if (GetWorld()->GetTimeSeconds() >= WindUpStart)
		Release();
}

void AGrenades::EjectPin(bool bTeeth) {
	const ADFCharacter* C = GetOwnerCharacter();
	if (!C || State == EDFWeaponState::Inactive)
		return;
	FVector Eye;
	FRotator View;
	C->GetActorEyesViewPoint(Eye, View);
	// Owner.Location + CalcDrawOffset() + PinEjectLoc (60, -10, -10) / PinEjectLoc2 (60, -5, -15),
	// out to the right from the teeth, to the left from the hand (uu here x 2.25)
	const EDFWeaponHand Hand = UDFUserSettings::Get().WeaponHand;
	const float Side = Hand == EDFWeaponHand::Left ? -1.f : 1.f;
	const FVector Pin = bTeeth ? FVector(60.f, -5.f, -15.f) : FVector(60.f, -10.f, -10.f);
	const FVector Draw = FVector(Pin.X + ArmsOffset.X, Side * (Pin.Y + ArmsOffset.Y), Pin.Z + ArmsOffset.Z);
	FDFCasing PinCase;
	PinCase.Mesh = TEXT("RageWeapons/GrenadePin");
	ADFShellCase::Eject(GetWorld(), PinCase, Eye, View, Draw * 2.25f, C->GetVelocity(), bTeeth ? Side : -Side);
}

void AGrenades::Release() {
	if (!bWinding)
		return;
	// MF: at least 1.5 on a throw, 3 on an alt (impact) throw
	const float Power = FMath::Max(EndWindUp(), bWindingAlt ? 3.f : ThrowReleaseMin);
	if (GetLocalRole() < ROLE_Authority)
		ServerThrow(bWindingAlt, Power);
	Throw(bWindingAlt, Power);
}

void AGrenades::ServerThrow_Implementation(bool bAlt, float Power) {
	Throw(bAlt, ClampReleasePower(Power));
}

void AGrenades::Throw(bool bAlt, float Power) {
	if (!IsSimulatingLocally() || !HasAmmoInClip())
		return;
	UseAmmo();
	SetState(EDFWeaponState::Firing, FireTime);
	if (HasAuthority()) {
		ShowShot(bAlt);
		SpawnProjectile(bAlt ? AltProjectileClass : ProjectileClass, Power * 337.5f, bAlt); // MF ThrowPower * 150 uu/s
	}
	if (IsLocallyControlled()) {
		PlayFireEffects(bAlt);
		GetWorldTimerManager().SetTimer(NextTimer, this, &AGrenades::NextGrenade, FireTime, false);
	}
}

void AGrenades::NextGrenade() {
	// PlayReloadingAnim: SelectUp at rate 1 and the select sound, while there is one to bring up
	UDFTweenAnimInstance* Anim = HasAmmoInClip() && !bWinding ? GetArmsAnim() : nullptr;
	if (!Anim || !Anim->Play(TEXT("SelectUp"), 1.f, 0.f, false))
		return;
	Anim->SetIdle(GetArmsAnimName(EDFWeaponState::Idle, false));
	PlaySoundLocal(SelectSound);
}

#undef LOCTEXT_NAMESPACE
