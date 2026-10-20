#include "Weapons/MFWeapon_TripBombs.h"
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

AMFWeapon_TripBombs::AMFWeapon_TripBombs()
{
	ViewMesh = TEXT("RageWeapons/TripBombsPickupCarryMesh");
	HUDIconRect = FIntRect(192, 0, 256, 64);   // WeaponIcon X=192 Y=0 W=64 H=64
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
	BombClass = AMFTripBomb::StaticClass();
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
	if (HasAuthority() && !CanPlace())
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
			SpawnProjectile(AltProjectileClass, GetThrowSpeed(Power), true);
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
	if (bNeedLaserTarget && !GetWorld()->LineTraceSingleByChannel(Opposite, BombLoc, BombLoc + Wall.ImpactNormal * MaxLaserDistance, ECC_Visibility, Params))
	{
		UGameplayStatics::PlaySoundAtLocation(this, LoadObject<USoundBase>(nullptr, TEXT("/MobileForcesClassic/Sounds/WeaponSFX_TripBombs/PlantFail.PlantFail"), nullptr, LOAD_NoWarn | LOAD_Quiet), BombLoc);
		return false;   // too far for the laser to work reliably
	}
	FActorSpawnParameters SP;
	SP.Owner = C;
	SP.Instigator = C;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AMFTripBomb* Bomb = GetWorld()->SpawnActor<AMFTripBomb>(BombClass ? *BombClass : AMFTripBomb::StaticClass(), BombLoc, Wall.ImpactNormal.Rotation(), SP))
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

#undef LOCTEXT_NAMESPACE
