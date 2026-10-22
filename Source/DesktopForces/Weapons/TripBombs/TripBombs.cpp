#include "Weapons/TripBombs/TripBombs.h"
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

ATripBombs::ATripBombs()
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
	AltProjectileClass = ATripMine::StaticClass();
	BombClass = ATripBomb::StaticClass();
	RecoilPower = 0.f;
	ClassicFireSound = TEXT("WeaponSFX_TripBombs.Plant");
	ClassicSelectSound = TEXT("WeaponSFX_TripBombs.Idle01");
}

float ATripBombs::GetThrowPower() const
{
	// MF: ThrowPower += Delta * 5 (max 10); +3 if released under 3
	float P = FMath::Min(10.f, 5.f * float(GetWorld()->GetTimeSeconds() - WindUpStart));
	return P < 3.f ? P + 3.f : P;
}

void ATripBombs::StartFire(bool bAlt)
{
	if (State != EDFWeaponState::Idle || !HasAmmoInClip())
	{
		return;
	}
	if (bAlt)
	{
		bWinding = true;
		WindUpStart = GetWorld()->GetTimeSeconds();
		SetAmbientMFSound(TEXT("WeaponSFX_TripBombs.Powering"));   // PlayAltFiringAnim: SlideBack
		return;
	}
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerUse(false, 0.f);
	}
	Use(false, 0.f);
}

void ATripBombs::StopFire(bool bAlt)
{
	if (!bAlt || !bWinding)
	{
		return;
	}
	bWinding = false;
	SetAmbientMFSound(nullptr);   // ClientThrowTripBomb
	const float Power = GetThrowPower();
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerUse(true, Power);
	}
	Use(true, Power);
}

void ATripBombs::ServerUse_Implementation(bool bAlt, float Power)
{
	Use(bAlt, FMath::Clamp(Power, 3.f, 10.f));
}

void ATripBombs::Use(bool bAlt, float Power)
{
	if (!IsSimulatingLocally() || !HasAmmoInClip() || State != EDFWeaponState::Idle)
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
	SetState(EDFWeaponState::Firing, FireTime);
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

bool ATripBombs::TryPlant()
{
	ADFCharacter* C = GetOwnerCharacter();
	if (!C)
	{
		return false;
	}
	// MF: trace 50 uu ahead; the laser must hit something within MaxLaserDistance
	const FVector Start = GetFireStart();
	const FVector Dir = GetAimRotation().Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFPlant), false, C);
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
	if (ATripBomb* Bomb = GetWorld()->SpawnActor<ATripBomb>(BombClass ? *BombClass : ATripBomb::StaticClass(), BombLoc, Wall.ImpactNormal.Rotation(), SP))
	{
		Bomb->MaxLaserDistance = MaxLaserDistance;
		Bomb->Team = C->GetTeam();
		return true;
	}
	return false;
}

FString ATripBombs::GetHUDStatus() const
{
	return bWinding ? FString::Printf(TEXT("THROW %d"), FMath::FloorToInt(GetThrowPower())) : FString();
}

#undef LOCTEXT_NAMESPACE
