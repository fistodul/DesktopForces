#include "Weapons/RocketLauncher/RocketLauncher.h"
#include "Sound/SoundBase.h"
#include "EngineUtils.h"
#include "Vehicles/DFVehicle.h"
#include "Core/DFTypes.h"
#include "Core/DFContent.h"
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

ARocketLauncher::ARocketLauncher()
{
	ViewMesh = TEXT("RageWeapons/RocketLauncherPickupCarryMesh");
	HUDIconRect = FIntRect(0, 128, 160, 192);   // WeaponIcon X=0 Y=128 W=160 H=64
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
	ProjectileClass = ARocket::StaticClass();
	AltProjectileClass = ARocket::StaticClass();
	MaxZoomLevel = 1;
	ZoomScale = 4.f;
	RecoilPower = 8.f;
	ClassicFireSound = TEXT("WeaponSFX_RocketLauncher.Fire");
	ClassicReloadSound = TEXT("WeaponSFX_RocketLauncher.Reload");
	ClassicSelectSound = TEXT("WeaponSFX_RocketLauncher.Select");
}

void ARocketLauncher::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ARocketLauncher, LockedTarget, COND_OwnerOnly);
}

FString ARocketLauncher::GetHUDStatus() const
{
	return LockedTarget ? TEXT("LOCKED") : Super::GetHUDStatus();
}

void ARocketLauncher::ToggleZoom()
{
	Super::ToggleZoom();
	if (!HasAuthority())
	{
		return;
	}
	LockedTarget = nullptr;
	LastSeen = nullptr;
	if (ZoomLevel > 0)
	{
		GetWorldTimerManager().SetTimer(LockTimer, this, &ARocketLauncher::CheckLock, 1.25f, true);
	}
	else
	{
		GetWorldTimerManager().ClearTimer(LockTimer);
	}
}

void ARocketLauncher::CheckLock()
{
	ADFCharacter* C = GetOwnerCharacter();
	if (!C || !C->IsAlive() || ZoomLevel == 0 || C->GetCurrentWeapon() != this)
	{
		LockedTarget = nullptr;
		LastSeen = nullptr;
		GetWorldTimerManager().ClearTimer(LockTimer);
		return;
	}
	AActor* Seen = FindTarget();
	const bool bWasLocked = LockedTarget != nullptr;
	LockedTarget = Seen && Seen == LastSeen.Get() ? Seen : nullptr;
	LastSeen = Seen;
	// Misc1Sound on lock, Misc2Sound when it's lost
	if (LockedTarget && !bWasLocked)
	{
		DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/WeaponSFX_RocketLauncher/Idle01")), GetActorLocation(), 1.f, 400.f);
	}
	else if (!LockedTarget && bWasLocked)
	{
		DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/WeaponSFX_RocketLauncher/Idle02")), GetActorLocation(), 1.f, 400.f);
	}
}

AActor* ARocketLauncher::FindTarget() const
{
	const ADFCharacter* Me = GetOwnerCharacter();
	const AController* PC = Me ? Me->GetController() : nullptr;
	if (!PC)
	{
		return nullptr;
	}
	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye, View);
	const FVector Dir = View.Vector();
	const uint8 MyTeam = Me->GetTeam();
	AActor* Best = nullptr;
	float BestAim = 0.93f;   // PlayerPawn.PickTarget(bestAim 0.93)
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		APawn* P = *It;
		const ADFCharacter* Soldier = Cast<ADFCharacter>(P);
		const ADFVehicle* Vehicle = Cast<ADFVehicle>(P);
		if (P == Me || (!Soldier && !Vehicle))
		{
			continue;
		}
		if (Soldier && (!Soldier->IsAlive() || Soldier->GetAttachParentActor()))
		{
			continue;   // riding: the vehicle is the target
		}
		if (Vehicle && (!Vehicle->GetDriver() || Vehicle == Me->GetAttachParentActor()))
		{
			continue;   // empty cars aren't worth a smart rocket
		}
		const uint8 Team = Soldier ? Soldier->GetTeam() : Vehicle->GetDriver()->GetTeam();
		if (MyTeam != DF::NoTeam && Team == MyTeam)
		{
			continue;
		}
		const FVector To = P->GetActorLocation() - Eye;
		const float Aim = FVector::DotProduct(To.GetSafeNormal(), Dir);
		if (Aim <= BestAim || To.Size() > MaxTraceDistance * 2.f)
		{
			continue;
		}
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DFRocketLock), false, Me);
		Params.AddIgnoredActor(P);
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, P->GetActorLocation(), ECC_Visibility, Params))
		{
			Best = P;
			BestAim = Aim;
		}
	}
	return Best;
}

ADFProjectile* ARocketLauncher::SpawnProjectile(TSubclassOf<ADFProjectile> Class, float Speed, bool bAlt)
{
	ADFProjectile* Rocket = Super::SpawnProjectile(Class, Speed, bAlt);
	if (Rocket && LockedTarget)
	{
		Rocket->HomeOn(LockedTarget, 0.08f);   // SmartRocket.StartSeekCount: 2 smoke puffs
		LockedTarget = nullptr;
		LastSeen = nullptr;
	}
	return Rocket;
}

#undef LOCTEXT_NAMESPACE
