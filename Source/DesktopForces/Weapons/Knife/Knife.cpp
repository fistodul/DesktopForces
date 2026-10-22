#include "Weapons/Knife/Knife.h"
#include "DesktopForces.h"
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

AKnife::AKnife()
{
	ViewMesh = TEXT("RageWeapons/KnifePickupCarryMesh");   // PickupViewMesh / ThirdPersonMesh
	ViewRotation = FRotator(-70.f, 0.f, 0.f);              // the blade stands up in the model: point it forward
	ViewOffset = FVector(16.f, 10.f, 9.f);
	HUDIconRect = FIntRect(96, 0, 144, 64);   // WeaponIcon X=96 Y=0 W=48 H=64
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
	DamageType = UDFDamage_Knife::StaticClass();
	AltProjectileClass = AThrownKnife::StaticClass();
	bInstantHit = true;
	bWallImpactFX = false;     // Slash: just the damage, no wall puff or ricochet
	RecoilPower = 0.f;
	ClassicFireSound = TEXT("WeaponSFX_Knife.Fire01");
	ClassicSelectSound = TEXT("WeaponSFX_Knife.Select");
}

void AKnife::FireShot(bool bAlt)
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
			FCollisionQueryParams Params(SCENE_QUERY_STAT(DFKnife), false, GetOwner());
			Params.AddIgnoredActor(GetOwner() ? GetOwner()->GetAttachParentActor() : nullptr);
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
	// alt: thrown on release (StopFire)
}

float AKnife::GetThrowPower() const
{
	return bWinding ? FMath::Clamp(8.f * float(GetWorld()->GetTimeSeconds() - WindUpStart), 0.f, 10.f) : 0.f;
}

void AKnife::StartFire(bool bAlt)
{
	if (!bAlt)
	{
		Super::StartFire(false);
		return;
	}
	// AltFire: ThrowPower 0, the arm pulls back (AltFireSound), then a release throws
	if (State != EDFWeaponState::Idle || !HasAmmoInClip() || bWinding)
	{
		return;
	}
	bWinding = true;
	bThrowQueued = false;
	WindUpStart = GetWorld()->GetTimeSeconds();
	PlayMFSound(TEXT("WeaponSFX_Knife.Throw"));
	GetWorldTimerManager().SetTimer(WindUpTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (bThrowQueued)
		{
			StopFire(true);   // let go during the pull-back: it flies once the arm is back
		}
	}), WindUpTime, false);
}

void AKnife::StopFire(bool bAlt)
{
	if (!bAlt)
	{
		Super::StopFire(false);
		return;
	}
	if (!bWinding)
	{
		return;
	}
	if (GetWorld()->GetTimeSeconds() - WindUpStart < WindUpTime)
	{
		bThrowQueued = true;
		return;
	}
	const float Power = GetThrowPower();
	bWinding = bThrowQueued = false;
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerThrow(Power);
	}
	Throw(Power);
}

void AKnife::ServerThrow_Implementation(float Power)
{
	Throw(FMath::Clamp(Power, 0.f, 10.f));
}

void AKnife::Throw(float Power)
{
	if (!IsSimulatingLocally() || !HasAmmoInClip())
	{
		return;
	}
	// ThrowKnife: the knife leaves the hand until it "returns"
	ClipAmmo = 0;
	bLastShotAlt = true;
	SetState(EDFWeaponState::Firing, FireTime);
	if (HasAuthority())
	{
		++BurstCounter;
		if (AThrownKnife* K = Cast<AThrownKnife>(SpawnProjectile(AltProjectileClass, GetThrowSpeed(Power), true)))
		{
			K->OwnerKnife = this;
			K->ReturnTime = KnifeReturnTime;
			GetThrowDamage(K->Damage, K->HeadDamage);
			K->DamageType = GetThrowDamageType();
		}
		// safety net only: the knife itself says when it's back
		GetWorldTimerManager().SetTimer(ReturnTimer, this, &AKnife::KnifeReturned, KnifeReturnTime + 30.f, false);
	}
	if (IsLocallyControlled())
	{
		PlayFireEffects(true);
	}
}

FString AKnife::GetHUDStatus() const
{
	return bWinding && ShowsThrowPower() ? FString::Printf(TEXT("THROW %d"), FMath::FloorToInt(GetThrowPower())) : FString();
}

void AKnife::PlayFireEffects(bool bAlt)
{
	// PlayFiringAnim: one of five slash / stab sounds (FireSounds[Rand(5)])
	static const TCHAR* Slashes[] = { TEXT("WeaponSFX_Knife.Fire"), TEXT("WeaponSFX_Knife.Fire01"), TEXT("WeaponSFX_Knife.Fire02"),
		TEXT("WeaponSFX_Knife.Fire03"), TEXT("WeaponSFX_Knife.Fire04") };
	if (bAlt)
	{
		Super::PlayFireEffects(bAlt);
		return;
	}
	const TObjectPtr<USoundBase> Single = FireSound;
	FireSound = nullptr;
	Super::PlayFireEffects(bAlt);
	FireSound = Single;
	PlayMFSound(Slashes[FMath::RandRange(0, 4)]);
}

void AKnife::Finish()
{
	// No reloading: an empty knife just idles until it comes back
	State = EDFWeaponState::Idle;
	if (bFireHeld && HasAmmoInClip())
	{
		TryFire(false);
	}
}

void AKnife::KnifeReturned()
{
	GetWorldTimerManager().ClearTimer(ReturnTimer);
	ClipAmmo = 1;
	UE_LOG(LogDF, Verbose, TEXT("%s: the thrown knife is back"), *GetName());
}

#undef LOCTEXT_NAMESPACE
