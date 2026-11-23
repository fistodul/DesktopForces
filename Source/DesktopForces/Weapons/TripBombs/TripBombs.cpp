#include "Weapons/TripBombs/TripBombs.h"
#include "Core/DFContent.h"
#include "Weapons/DFProjectile.h"
#include "Weapons/TripBombs/TripBomb.h"
#include "Player/DFCharacterMovement.h"
#include "Kismet/GameplayStatics.h"
#include "Player/DFCharacter.h"
#include "Player/DFSoldierAnimInstance.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "DFWeapons"

ATripBombs::ATripBombs() {
	ThrowChargeRate = 5.f; // AltFiring.Tick: ThrowPower += Delta * 5
	ThrowReleaseFloor = 3.f; // if (ThrowPower < 3) ThrowPower += 3 on release
	DeathMessage = NSLOCTEXT("DFKills", "TripBombs", "%k Blew Up %o.");
	ViewMesh = TEXT("RageWeapons/TripBombsPickupCarryMesh");
	ArmsMesh = TEXT("TripBombsPlayerMesh"); // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(2.f, 0.2f, -1.f);
	HUDIconRect = FIntRect(192, 0, 256, 64); // WeaponIcon X=192 Y=0 W=64 H=64
	DisplayName = LOCTEXT("TripBombs", "Trip Bombs");
	InventoryGroup = 9;
	CarrySize = 1;
	MaxCanCarry = 3;
	AIRating = 0.4f;
	MaxClipAmmo = 2;
	MaxClips = 1;
	bDestroyWhenEmpty = true;
	FireTime = 0.967f; // PlaceForward 29 @30
	EquipTime = 0.6f; // SelectUp 19 @30
	PutDownTime = 0.667f;
	bInstantHit = false;
	AltProjectileClass = ATripMine::StaticClass();
	BombClass = ATripBomb::StaticClass();
	RecoilPower = 0.f;
	ClassicFireSound = TEXT("WeaponSFX_TripBombs.Plant");
	ClassicSelectSound = TEXT("WeaponSFX_HeavyMachineGun.Select"); // TripBombs.SelectSound
	SetIdleFidgets(TEXT("WeaponSFX_TripBombs"));
}

void ATripBombs::PlayArms(FName Name, float Duration) {
	if (UDFTweenAnimInstance* Anim = IsLocallyControlled() ? GetArmsAnim() : nullptr) {
		Anim->SetIdle(GetArmsAnimName(EDFWeaponState::Idle, false), 0.1f);
		Anim->PlayFitted(Name, Duration, 0.05f);
	}
}

void ATripBombs::BusyFor(float Seconds, void (ATripBombs::*Then)()) {
	// a step of NormalFire / AltFiring: the weapon stays Firing until the last one Finish()es
	State = EDFWeaponState::Firing;
	ClearStateTimer();
	GetWorldTimerManager().SetTimer(StepTimer, this, Then, FMath::Max(Seconds, 0.01f), false);
}

void ATripBombs::StartFire(bool bAlt) {
	const ADFCharacter* C = GetOwnerCharacter();
	if (State != EDFWeaponState::Idle || !HasAmmoInClip() || !C || !C->IsAlive() || C->IsInLoadout())
		return;
	if (bAlt) {
		BeginWindUp();
		return;
	}
	if (GetLocalRole() < ROLE_Authority)
		ServerPlace();
	BeginPlace();
}

void ATripBombs::StopFire(bool bAlt) {
	if (!bAlt || !bWinding)
		return;
	bReleased = true;
	if (GetWorld()->GetTimeSeconds() >= WindUpStart) // the hand is back: throw now, else at SlideBack's end
		Throw();
}

void ATripBombs::ServerPlace_Implementation() {
	if (State == EDFWeaponState::Idle && HasAmmoInClip())
		BeginPlace();
}

void ATripBombs::BeginPlace() {
	// TripBombs.Fire -> NormalFire: PlayFiringAnim PlaceForward (rate 0.9), and only at its end
	// (AnimEnd) TryAndStick: the reach forward is the test of whether there is a wall to take it
	PlayArms(TEXT("PlaceForward"), PlaceTime);
	BusyFor(PlaceTime, &ATripBombs::EndPlace);
}

void ATripBombs::EndPlace() {
	bool bStuck;
	if (HasAuthority()) {
		bStuck = CanPlace() && TryPlant();
		if (bStuck) {
			UseAmmo();
			ShowShot(false);
		}
	} else {
		bStuck = CanPlant(); // ClientFinish: the same test, the server's bomb follows
		if (bStuck)
			UseAmmo();
	}
	// PlayTripBombPlant (PlaceBack + Plant) / PlayTripBombFailToPlant (PlaceBombDown + PlantFail)
	if (IsLocallyControlled()) {
		if (bStuck)
			PlayBodyFire(false); // the pawn's plant (PlayFiring)
		PlayArms(bStuck ? TEXT("PlaceBack") : TEXT("PlaceBombDown"), PlaceBackTime);
		PlayMFSound(bStuck ? TEXT("WeaponSFX_TripBombs.Plant") : TEXT("WeaponSFX_TripBombs.PlantFail"));
	}
	BusyFor(PlaceBackTime, &ATripBombs::Done);
}

void ATripBombs::BeginWindUp() {
	// AltFire -> AltFiring: PlayAltFiringAnim SlideBack + the Powering hum; the throw charges
	// once the hand is back (FinishAnim, then Tick)
	StartWindUp(SlideBackTime); // MF: ThrowPower += Delta * 5 once SlideBack has finished
	bReleased = false;
	SetAmbientMFSound(TEXT("WeaponSFX_TripBombs.Powering"));
	PlayArms(TEXT("SlideBack"), SlideBackTime);
	BusyFor(SlideBackTime, &ATripBombs::WindUpReady);
}

void ATripBombs::WindUpReady() {
	if (bReleased)
		Throw();
}

void ATripBombs::Throw() {
	if (!bWinding)
		return;
	bWinding = false;
	SetAmbientMFSound(nullptr);
	const float Power = GetReleasePower();
	if (GetLocalRole() < ROLE_Authority)
		ServerThrow(Power);
	if (HasAuthority() ? ThrowNow(Power) : true)
		UseAmmo(); // the server's count wins if the throw was refused there
	// ClientThrowTripBomb: SlideForward and a sound - the throw's own (Filip: not the plant's,
	// MF's FireSound)
	PlayArms(TEXT("SlideForward"), SlideForwardTime);
	if (IsLocallyControlled())
		PlayBodyFire(true); // the pawn's throw (PlayAltFiring)
	if (IsLocallyControlled())
		PlayMFSound(TEXT("WeaponSFX_TripBombs.Throw"));
	BusyFor(SlideForwardTime, &ATripBombs::Done);
}

void ATripBombs::ServerThrow_Implementation(float Power) {
	if (HasAmmoInClip() && ThrowNow(ClampReleasePower(Power))) {
		UseAmmo(); // a remote thrower's: the server's copy of the weapon goes the same way
		BusyFor(SlideForwardTime, &ATripBombs::Done);
	}
}

bool ATripBombs::ThrowNow(float Power) {
	if (!CanPlace())
		return false;
	ShowShot(true);
	SpawnProjectile(AltProjectileClass, GetThrowSpeed(Power), true);
	return true;
}

void ATripBombs::Done() {
	Finish(); // NormalFire2 / AltFiring AnimEnd
}

void ATripBombs::ForceInactive() {
	GetWorldTimerManager().ClearTimer(StepTimer);
	bWinding = bReleased = false;
	SetAmbientMFSound(nullptr);
	Super::ForceInactive();
}

bool ATripBombs::CanPlant() const {
	// ClientTestForStick / TryAndStick's test: a wall 50 uu ahead, and for the laser a surface
	// within MaxLaserDistance across from it
	const ADFCharacter* C = GetOwnerCharacter();
	if (!C)
		return false;
	const FVector Start = GetFireStart();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFPlant), false, C);
	FHitResult Wall, Opposite;
	if (!GetWorld()->LineTraceSingleByChannel(Wall, Start, Start + GetAimRotation().Vector() * PlaceReach, ECC_Visibility, Params) || Cast<APawn>(Wall.GetActor()))
		return false;
	const FVector BombLoc = Wall.ImpactPoint + Wall.ImpactNormal * 6.f;
	return !bNeedLaserTarget || GetWorld()->LineTraceSingleByChannel(Opposite, BombLoc, BombLoc + Wall.ImpactNormal * MaxLaserDistance, ECC_Visibility, Params);
}

bool ATripBombs::TryPlant() {
	ADFCharacter* C = GetOwnerCharacter();
	if (!C || !CanPlant())
		return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFPlant), false, C);
	FHitResult Wall;
	const FVector Start = GetFireStart();
	GetWorld()->LineTraceSingleByChannel(Wall, Start, Start + GetAimRotation().Vector() * PlaceReach, ECC_Visibility, Params);
	FActorSpawnParameters SP;
	SP.Owner = C;
	SP.Instigator = C;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ATripBomb* Bomb = GetWorld()->SpawnActor<ATripBomb>(BombClass ? *BombClass : ATripBomb::StaticClass(), Wall.ImpactPoint + Wall.ImpactNormal * 6.f, Wall.ImpactNormal.Rotation(), SP)) {
		Bomb->MaxLaserDistance = MaxLaserDistance;
		Bomb->Team = C->GetTeam();
		return true;
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
