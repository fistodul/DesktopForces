#include "MFSentryGun.h"
#include "Core/MFGameSettings.h"
#include "Core/MFProps.h"
#include "Player/MFCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MFModSentryGun"

namespace
{
	void PlayAt(const AActor* A, const TCHAR* Path)
	{
		UGameplayStatics::PlaySoundAtLocation(A, LoadObject<USoundBase>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet), A->GetActorLocation());
	}
	const TCHAR* ActivatingSound = TEXT("/Game/MFImported/Sounds/WeaponSFX_TripBombs/Activating.Activating");
	const TCHAR* FireSound = TEXT("/Game/MFImported/Sounds/WeaponSFX_HeavyMachineGun/Fire.Fire");
	const TCHAR* SpinDownSound = TEXT("/Game/MFImported/Sounds/WeaponSFX_HeavyMachineGun/SpinDown.SpinDown");
	const TCHAR* ExplodeSound = TEXT("/Game/MFImported/Sounds/MiscSFX/Explode.Explode");
}

UMFDamage_Sentry::UMFDamage_Sentry()
{
	DeathVerb = LOCTEXT("Shredded", "shredded");
}

// --- turret -------------------------------------------------------------------------------

AMFSentryTurret::AMFSentryTurret()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	InitialLifeSpan = 480.f;

	// MF CollisionRadius 20 uu; blocks players and shots
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->InitBoxExtent(FVector(45.f, 45.f, 50.f));
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	RootComponent = Collision;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	// placeholders until the HMG model is converted: a stand and a gun box
	Tripod = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Tripod"));
	Tripod->SetupAttachment(Collision);
	Tripod->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Tripod->SetRelativeLocation(FVector(0.f, 0.f, -20.f));
	Tripod->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.6f));
	Gun = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Gun"));
	Gun->SetupAttachment(Collision);
	Gun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Gun->SetRelativeLocation(FVector(0.f, 0.f, 22.5f));
	Gun->SetRelativeScale3D(FVector(0.9f, 0.25f, 0.25f));
	if (Cylinder.Succeeded())
	{
		Tripod->SetStaticMesh(Cylinder.Object);
	}
	if (Cube.Succeeded())
	{
		Gun->SetStaticMesh(Cube.Object);
	}
}

void AMFSentryTurret::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFSentryTurret, Aim);
	DOREPLIFETIME(AMFSentryTurret, Team);
}

void AMFSentryTurret::InitPlacer(AMFCharacter* Placer)
{
	SetInstigator(Placer);
	Team = Placer ? Placer->GetTeam() : MF::NoTeam;
	Aim = GetActorRotation();
}

void AMFSentryTurret::BeginPlay()
{
	Super::BeginPlay();
	// MF SentryGunTurret draws the HMG carry model (DrawScale 1.25); team tint on the placeholder only
	if (MF::ApplyPropMesh(Gun, TEXT("RageWeapons/HeavyMachineGunPickupCarryMesh"), 1.25f, FVector(0.f, 0.f, 22.5f)))
	{
		Gun->SetWorldRotation(Aim);
	}
	else if (GetNetMode() != NM_DedicatedServer)
	{
		if (UMaterialInterface* M = UMFGameSettings::Get().GetTeamTintMaterial())
		{
			Gun->SetMaterial(0, M);
			const FLinearColor C = Team == MF::NoTeam ? FLinearColor(0.9f, 0.7f, 0.15f) : UMFGameSettings::Get().GetTeamColor(Team);
			Gun->SetCustomPrimitiveDataVector4(0, FVector4(C.R, C.G, C.B, 1.f));
		}
	}
	if (HasAuthority())
	{
		Ammo = MaxAmmo;
		PlayAt(this, ActivatingSound);
		// state Deploying: 0.4 s, then Scanning at 5 Hz
		GetWorldTimerManager().SetTimer(Timer, this, &AMFSentryTurret::Scan, 0.2f, true, 0.4f);
	}
}

void AMFSentryTurret::OnRep_Aim()
{
	Gun->SetWorldRotation(Aim);
}

bool AMFSentryTurret::IsFriendly(const AActor* Other) const
{
	if (const AMFSentryTurret* S = Cast<AMFSentryTurret>(Other))
	{
		return S == this || (Team != MF::NoTeam ? S->Team == Team : S->GetInstigator() == GetInstigator());
	}
	const AMFCharacter* C = Cast<AMFCharacter>(Other);
	if (!C || !C->IsAlive())
	{
		return true;
	}
	return C == GetInstigator() || (Team != MF::NoTeam && C->GetTeam() == Team);
}

FVector AMFSentryTurret::Muzzle() const
{
	return GetActorLocation() + FVector(0.f, 0.f, 22.5f);
}

bool AMFSentryTurret::CanSee(const AActor* A) const
{
	// GetTargetAimPoint: middle of a player, low on a turret
	const FVector To = A->IsA<AMFCharacter>() ? A->GetActorLocation() + FVector(0.f, 0.f, 30.f) : A->GetActorLocation() + FVector(0.f, 0.f, 34.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFSentrySight), false, this);
	Params.AddIgnoredActor(A);
	return !GetWorld()->LineTraceTestByChannel(Muzzle(), To, ECC_Visibility, Params);
}

AActor* AMFSentryTurret::FindBestTarget() const
{
	AActor* Best = nullptr;
	float BestDist = ScanRange;
	auto Consider = [&](AActor* A)
	{
		const float D = FVector::Dist(A->GetActorLocation(), GetActorLocation());
		if (D < BestDist && !IsFriendly(A) && CanSee(A))
		{
			BestDist = D;
			Best = A;
		}
	};
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
	{
		Consider(*It);
	}
	for (TActorIterator<AMFSentryTurret> It(GetWorld()); It; ++It)
	{
		if (!It->bDead)
		{
			Consider(*It);
		}
	}
	return Best;
}

void AMFSentryTurret::Scan()
{
	if (bDead)
	{
		return;
	}
	if (Ammo <= 0)
	{
		OutOfAmmo();
		return;
	}
	if (AActor* T = FindBestTarget())
	{
		Target = T;
		PlayAt(this, ActivatingSound);
		CurrentInterval = InitialFireInterval;
		NextShot = 0.f;
		GetWorldTimerManager().SetTimer(Timer, this, &AMFSentryTurret::FireTick, 0.05f, true);
	}
}

void AMFSentryTurret::FireTick()
{
	AActor* T = Target.Get();
	if (!T || IsFriendly(T) || FVector::Dist(T->GetActorLocation(), GetActorLocation()) > ScanRange || !CanSee(T))
	{
		T = FindBestTarget();
		Target = T;
		if (!T)
		{
			// back to scanning; it winds down if it had spun up
			if (CurrentInterval < InitialFireInterval)
			{
				PlayAt(this, SpinDownSound);
			}
			GetWorldTimerManager().SetTimer(Timer, this, &AMFSentryTurret::Scan, 0.2f, true);
			return;
		}
	}
	// UpdateTurretRotation: TurnRate deg/s towards the target, fire when within 15 degrees
	const FVector AimPoint = T->IsA<AMFCharacter>() ? T->GetActorLocation() + FVector(0.f, 0.f, 30.f) : T->GetActorLocation() + FVector(0.f, 0.f, 34.f);
	const FRotator Want = (AimPoint - Muzzle()).Rotation();
	const float Step = TurnRate * 0.05f;
	const FRotator Delta = (Want - Aim).GetNormalized();
	Aim.Yaw += FMath::Clamp(Delta.Yaw, -Step, Step);
	Aim.Pitch += FMath::Clamp(Delta.Pitch, -Step, Step);
	Aim.Roll = 0.f;
	Aim.Normalize();
	OnRep_Aim();
	const float Now = GetWorld()->GetTimeSeconds();
	if (FMath::Abs(Delta.Yaw) < 15.f && FMath::Abs(Delta.Pitch) < 15.f && Now >= NextShot)
	{
		FireShot();
		CurrentInterval = FMath::Max(FireInterval, CurrentInterval - FireIntervalStep);
		NextShot = Now + CurrentInterval;
		if (Ammo <= 0)
		{
			OutOfAmmo();
		}
	}
}

AController* AMFSentryTurret::Credit() const
{
	// GetDamageInstigator: the placer gets the kills while still on the placing team
	const AMFCharacter* Placer = Cast<AMFCharacter>(GetInstigator());
	return Placer && (Team == MF::NoTeam || Placer->GetTeam() == Team) ? Placer->GetController() : nullptr;
}

void AMFSentryTurret::FireShot()
{
	--Ammo;
	const FVector Start = Muzzle();
	const FRotationMatrix M(Aim);
	const FVector Dir = (M.GetUnitAxis(EAxis::X) + (FMath::FRand() - 0.5f) * Spread * M.GetUnitAxis(EAxis::Y)
		+ (FMath::FRand() - 0.5f) * Spread * M.GetUnitAxis(EAxis::Z)).GetSafeNormal();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFSentryShot), false, this);
	PlayAt(this, FireSound);
	MakeNoise(1.f, GetInstigator());
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Dir * ScanRange, ECC_GameTraceChannel2, Params))
	{
		return;
	}
	AActor* Other = Hit.GetActor();
	if (!Other || (IsFriendly(Other) && (Other->IsA<AMFCharacter>() || Other->IsA<AMFSentryTurret>())))
	{
		return;   // ProcessTraceHit: friends take nothing
	}
	FPointDamageEvent Ev(ShotDamage, Hit, Dir, UMFDamage_Sentry::StaticClass());
	Other->TakeDamage(ShotDamage, Ev, Credit(), this);
}

void AMFSentryTurret::OutOfAmmo()
{
	// state OutOfAmmo: spin down, gone 0.7 s later
	GetWorldTimerManager().ClearTimer(Timer);
	PlayAt(this, SpinDownSound);
	SetLifeSpan(0.7f);
}

float AMFSentryTurret::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (HasAuthority() && !bDead && DamageAmount > 0.f)
	{
		Health -= FMath::CeilToInt(DamageAmount);
		if (Health <= 0)
		{
			BlowUp();
		}
	}
	return DamageAmount;
}

void AMFSentryTurret::BlowUp()
{
	bDead = true;
	GetWorldTimerManager().ClearTimer(Timer);
	const FVector At = GetActorLocation() + FVector(0.f, 0.f, 22.5f);
	TArray<AActor*> Ignore{ this };
	UGameplayStatics::ApplyRadialDamageWithFalloff(this, 85.f, 0.f, At, 0.f, 562.5f, 1.f,
		UMFDamage_Sentry::StaticClass(), Ignore, this, Credit(), ECC_Visibility);   // MF HurtRadius(85, 250)
	PlayAt(this, ExplodeSound);
	Destroy();
}

// --- the item -------------------------------------------------------------------------------

AMFWeapon_SentryGun::AMFWeapon_SentryGun()
{
	DisplayName = LOCTEXT("SentryGun", "Sentry Gun");
	ViewMesh = TEXT("RageWeapons/HeavyMachineGunPickupCarryMesh");
	InventoryGroup = 11;
	CarrySize = 2;
	MaxClipAmmo = 1;
	MaxClips = 1;
	bDestroyWhenEmpty = true;
	AIRating = 0.7f;
	AutoSwitchPriority = 0.f;
	FireTime = 0.6f;
	RecoilPower = 0.f;
	bAddToLoadout = true;
	// Icon converted from the mod's own art (SourceArt/Icons/SentryIcon.bmp, 128 x 64)
	HUDIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/MFModSentryGun/Generated/Icons/SentryIcon.SentryIcon")));
	HUDIconRect = FIntRect(0, 0, 128, 64);
}

void AMFWeapon_SentryGun::Message(const FString& Text) const
{
	const AMFCharacter* Me = GetOwnerCharacter();
	if (APlayerController* PC = Me ? Cast<APlayerController>(Me->GetController()) : nullptr)
	{
		PC->ClientMessage(Text);
	}
}

bool AMFWeapon_SentryGun::FindPlacementSpot(FVector& OutLoc, FRotator& OutRot) const
{
	// SentryGun.FindPlacementSpot: flat ground 40-140 uu ahead where you look, else just in front
	const AMFCharacter* Me = GetOwnerCharacter();
	const FRotator Flat(0.f, GetAimRotation().Yaw, 0.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFSentryPlace), false, Me);
	FHitResult Hit;
	const FVector Eye = GetFireStart();
	if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + GetAimRotation().Vector() * 337.5f, ECC_Visibility, Params)
		&& Hit.ImpactNormal.Z > 0.65f)
	{
		const float D = FVector::Dist2D(Hit.ImpactPoint, Me->GetActorLocation());
		if (D >= 90.f && D <= 315.f)
		{
			OutLoc = Hit.ImpactPoint;
			OutRot = Flat;
			return true;
		}
	}
	const FVector Ahead = Me->GetActorLocation() + Flat.Vector() * 168.75f;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Ahead + FVector(0.f, 0.f, 45.f), Ahead - FVector(0.f, 0.f, 270.f), ECC_Visibility, Params)
		&& Hit.ImpactNormal.Z > 0.65f)
	{
		OutLoc = Hit.ImpactPoint;
		OutRot = Flat;
		return true;
	}
	return false;
}

bool AMFWeapon_SentryGun::TryFire(bool bAlt)
{
	AMFCharacter* Me = GetOwnerCharacter();
	if (!Me || !Me->IsAlive() || Me->IsInLoadout() || !HasAmmoInClip())
	{
		return false;
	}
	if (!HasAuthority())
	{
		SetState(EMFWeaponState::Firing, FireTime);
		return true;
	}
	int32 Active = 0;
	for (TActorIterator<AMFSentryTurret> It(GetWorld()); It; ++It)
	{
		Active += It->GetInstigator() == Me && !It->IsActorBeingDestroyed();
	}
	if (Active >= MaxActiveSentries)
	{
		Message(FString::Printf(TEXT("Max active Sentry Guns reached: %d"), MaxActiveSentries));
		return false;
	}
	FVector Loc;
	FRotator Rot;
	if (!FindPlacementSpot(Loc, Rot))
	{
		Message(TEXT("Cannot place Sentry Gun here (clear flat ground required)"));
		return false;
	}
	Loc.Z += 55.f;   // box half height + a little, so the spawn test clears the floor
	FActorSpawnParameters SP;
	SP.Instigator = Me;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
	AMFSentryTurret* Turret = GetWorld()->SpawnActorDeferred<AMFSentryTurret>(AMFSentryTurret::StaticClass(), FTransform(Rot, Loc), nullptr, Me,
		ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding);
	if (Turret)
	{
		Turret->InitPlacer(Me);
		UGameplayStatics::FinishSpawningActor(Turret, FTransform(Rot, Loc));
	}
	if (!IsValid(Turret))
	{
		Message(TEXT("Cannot deploy Sentry Gun (space obstructed)"));
		return false;
	}
	SetState(EMFWeaponState::Firing, FireTime);
	ClipAmmo = 0;   // Finish() hands the empty item back
	return true;
}

#undef LOCTEXT_NAMESPACE
