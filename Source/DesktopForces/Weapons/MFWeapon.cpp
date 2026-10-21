#include "Weapons/MFWeapon.h"
#include "Game/MFGameMode.h"
#include "Core/MFProps.h"
#include "Core/MFContent.h"
#include "Weapons/MFProjectile.h"
#include "Player/MFCharacter.h"
#include "DesktopForces.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

// Trace channel "Weapon" (Config/DefaultEngine.ini, ECC_GameTraceChannel2)
static constexpr ECollisionChannel ECC_MFWeapon = ECC_GameTraceChannel2;

AMFWeapon::AMFWeapon()
{
	PrimaryActorTick.bCanEverTick = true;           // falls when dropped, nothing else
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(10.f);
	SetMinNetUpdateFrequency(2.f);
	NetPriority = 1.5f;

	Mesh1P = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh1P"));
	Mesh1P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->CastShadow = false;
	Mesh1P->bReceivesDecals = false;
	Mesh1P->SetGenerateOverlapEvents(false);
	Mesh1P->PrimaryComponentTick.bCanEverTick = false;
	// the mesh hangs off a plain root: held, the root snaps to the camera socket and the mesh
	// keeps its view placement; dropped, it lies at the root
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Mesh1P->SetupAttachment(RootComponent);

	// Placeholder gun: a stretched cube in front of the camera.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Mesh1P->SetStaticMesh(Cube.Object);
		Mesh1P->SetRelativeScale3D(FVector(0.35f, 0.06f, 0.08f));
		Mesh1P->SetRelativeLocation(FVector(30.f, 14.f, -16.f));
	}

	DamageType = UMFDamage_Bullet::StaticClass();
	HeadshotDamageType = UMFDamage_Headshot::StaticClass();
	// MF's shared icon sheet (converted by Tools/mfport); each weapon sets its rect
	HUDIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/MobileForcesClassic/Textures/RageWeapons/WeaponIcons.WeaponIcons")));
	SetActorHiddenInGame(true);
}

void AMFWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AMFWeapon, ClipAmmo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AMFWeapon, TotalAmmo, COND_OwnerOnly);
	DOREPLIFETIME(AMFWeapon, ZoomLevel);
	DOREPLIFETIME(AMFWeapon, bDropped);
	DOREPLIFETIME_CONDITION(AMFWeapon, BurstCounter, COND_SkipOwner);
}

void AMFWeapon::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (HasAuthority())
	{
		GiveFullAmmo();   // RageWeapon.PostBeginPlay
	}
	ResolveSounds();
	if (!ViewMesh.IsEmpty() && MF::ApplyPropMesh(Mesh1P, *ViewMesh))
	{
		// view-model placement from the mesh's bounds: rear ViewOffset.X in front of the eye,
		// centred ViewOffset.Y to the right, top ViewOffset.Z below, at ViewScale
		const FBox B = Mesh1P->GetStaticMesh()->GetBoundingBox();
		const FVector Loc = ViewOffset + FVector(-B.Min.X, -B.GetCenter().Y, -B.Max.Z) * ViewScale;
		HandTransform = FTransform(ViewRotation, Loc, FVector(ViewScale));
		bHasViewMesh = true;
		UpdateDroppedLook();
	}
}

void AMFWeapon::Destroyed()
{
	ClearStateTimer();
	Super::Destroyed();
}

void AMFWeapon::ResolveSounds()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// Converted original sounds live in /MobileForcesClassic/Sounds/<Package>/<Name> (see Tools/mfport).
	auto Load = [](TObjectPtr<USoundBase>& Slot, const FString& MFName)
	{
		if (Slot || MFName.IsEmpty())
		{
			return;
		}
		FString Package, Name;
		if (MFName.Split(TEXT("."), &Package, &Name))
		{
			const FString Path = FString::Printf(TEXT("/MobileForcesClassic/Sounds/%s/%s.%s"), *Package, *Name, *Name);
			Slot = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
	};
	Load(FireSound, MFFireSound);
	Load(ReloadSound, MFReloadSound);
	Load(SelectSound, MFSelectSound);
}

// --- ammo ----------------------------------------------------------------------------

void AMFWeapon::GiveFullAmmo()
{
	ClipAmmo = MaxClipAmmo;
	TotalAmmo = MaxClips * MaxClipAmmo - MaxClipAmmo;
}

void AMFWeapon::ReloadAmmo()
{
	const int32 ToFill = FMath::Min(MaxClipAmmo - ClipAmmo, TotalAmmo);
	if (ToFill > 0)
	{
		TotalAmmo -= ToFill;
		ClipAmmo += ToFill;
	}
}

// --- helpers -------------------------------------------------------------------------

AMFCharacter* AMFWeapon::GetOwnerCharacter() const
{
	return Cast<AMFCharacter>(GetOwner());
}

bool AMFWeapon::IsLocallyControlled() const
{
	const APawn* P = Cast<APawn>(GetOwner());
	return P && P->IsLocallyControlled();
}

bool AMFWeapon::IsSimulatingLocally() const
{
	return HasAuthority() || IsLocallyControlled();
}

FVector AMFWeapon::GetFireStart() const
{
	const AMFCharacter* C = GetOwnerCharacter();
	return C ? C->GetPawnViewLocation() : GetActorLocation();
}

FRotator AMFWeapon::GetAimRotation() const
{
	const AMFCharacter* C = GetOwnerCharacter();
	return C ? C->GetBaseAimRotation() : GetActorRotation();
}

float AMFWeapon::GetFOVScale() const
{
	return ZoomLevel > 0 ? 1.f / FMath::Max(ZoomScale, 1.f) : 1.f;
}

float AMFWeapon::GetMoveSpeedScale() const
{
	return ZoomLevel > 0 ? ZoomedMoveScale : 1.f;
}

FString AMFWeapon::GetHUDStatus() const
{
	return FString();
}

void AMFWeapon::PlaySoundLocal(USoundBase* Sound) const
{
	if (Sound && GetNetMode() != NM_DedicatedServer)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, GetFireStart());
	}
}

// --- state machine -------------------------------------------------------------------

void AMFWeapon::SetState(EMFWeaponState NewState, float Duration)
{
	State = NewState;
	ClearStateTimer();
	if (Duration > 0.f)
	{
		GetWorldTimerManager().SetTimer(StateTimer, this, &AMFWeapon::OnStateTimer, Duration, false);
	}
	else if (NewState != EMFWeaponState::Idle && NewState != EMFWeaponState::Inactive)
	{
		// no animation (MF items with empty Play*Anim): done on the next frame
		StateTimer = GetWorldTimerManager().SetTimerForNextTick(this, &AMFWeapon::OnStateTimer);
	}
}

void AMFWeapon::ClearStateTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StateTimer);
	}
}

void AMFWeapon::OnStateTimer()
{
	switch (State)
	{
	case EMFWeaponState::Equipping:
		State = EMFWeaponState::Idle;
		Finish();
		break;
	case EMFWeaponState::Firing:
	case EMFWeaponState::Zooming:
		Finish();
		break;
	case EMFWeaponState::Reloading:
		ReloadAmmo();
		Finish();
		break;
	case EMFWeaponState::PuttingDown:
		ForceInactive();
		if (AMFCharacter* C = GetOwnerCharacter())
		{
			C->OnWeaponPutDown(this);
		}
		break;
	default:
		break;
	}
}

void AMFWeapon::Finish()
{
	AMFCharacter* C = GetOwnerCharacter();
	State = EMFWeaponState::Idle;
	if (!C || !C->IsAlive())
	{
		return;
	}

	if (bDestroyWhenEmpty && !HasAnyAmmo())
	{
		// MF: single-use items must leave the hands right away (loadout-consumables 6B)
		if (HasAuthority())
		{
			OnDepleted();
		}
		return;
	}

	if (bPendingReload || (!HasAmmoInClip() && CanReload()))
	{
		bPendingReload = false;
		StartReload();
		return;
	}

	if (!HasAmmoInClip())
	{
		if (HasAuthority())
		{
			C->SwitchToBestWeapon();
		}
		return;
	}

	if (bFireHeld)
	{
		TryFire(false);
	}
	else if (bAltFireHeld && MaxZoomLevel == 0)
	{
		TryFire(true);
	}
}

// --- input entry points --------------------------------------------------------------

void AMFWeapon::StartFire(bool bAlt)
{
	(bAlt ? bAltFireHeld : bFireHeld) = true;
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerStartFire(bAlt);
	}
	if (!IsSimulatingLocally() || State != EMFWeaponState::Idle)
	{
		return;
	}
	if (bAlt && MaxZoomLevel > 0)
	{
		ToggleZoom();
	}
	else
	{
		TryFire(bAlt);
	}
}

void AMFWeapon::StopFire(bool bAlt)
{
	(bAlt ? bAltFireHeld : bFireHeld) = false;
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerStopFire(bAlt);
	}
}

void AMFWeapon::ServerStartFire_Implementation(bool bAlt)
{
	StartFire(bAlt);
}

void AMFWeapon::ServerStopFire_Implementation(bool bAlt)
{
	StopFire(bAlt);
}

void AMFWeapon::StartReload()
{
	if (GetLocalRole() < ROLE_Authority && IsLocallyControlled() && State == EMFWeaponState::Idle)
	{
		ServerReload();
	}
	if (!IsSimulatingLocally() || !CanReload())
	{
		return;
	}
	if (State != EMFWeaponState::Idle)
	{
		return;
	}
	if (ZoomLevel > 0)
	{
		ZoomLevel = 0;
		if (AMFCharacter* C = GetOwnerCharacter())
		{
			C->UpdateCarrySpeed();
		}
	}
	SetState(EMFWeaponState::Reloading, ReloadTime);
	if (IsLocallyControlled())
	{
		PlaySoundLocal(ReloadSound);
	}
}

void AMFWeapon::ServerReload_Implementation()
{
	StartReload();
}

bool AMFWeapon::TryFire(bool bAlt)
{
	AMFCharacter* C = GetOwnerCharacter();
	if (!C || !C->IsAlive() || C->IsInLoadout())
	{
		return false;
	}
	if (!HasAmmoInClip())
	{
		if (CanReload())
		{
			StartReload();
		}
		return false;
	}
	const float Duration = ZoomLevel > 0 ? ZoomedFireTime : FireTime;
	SetState(EMFWeaponState::Firing, Duration);
	FireShot(bAlt);
	return true;
}

void AMFWeapon::ToggleZoom()
{
	ZoomLevel = (ZoomLevel + 1) % (MaxZoomLevel + 1);
	SetState(EMFWeaponState::Zooming, ZoomTime);
	if (AMFCharacter* C = GetOwnerCharacter())
	{
		C->UpdateCarrySpeed();
	}
	if (IsLocallyControlled())
	{
		PlaySoundLocal(ZoomLevel > 0 ? ZoomInSound : ZoomOutSound);
	}
}

void AMFWeapon::OnRep_ZoomLevel(uint8 OldZoom)
{
	if (AMFCharacter* C = GetOwnerCharacter())
	{
		C->UpdateCarrySpeed();
	}
}

// --- firing --------------------------------------------------------------------------

void AMFWeapon::FireShot(bool bAlt)
{
	ClipAmmo = FMath::Max(0, ClipAmmo - 1);
	bLastShotAlt = bAlt;

	if (HasAuthority())
	{
		++BurstCounter;
		const bool bZoomed = ZoomLevel > 0;
		const float UseAccuracy = (bAlt || bZoomed) ? AltAccuracy : Accuracy;
		TSubclassOf<AMFProjectile> Proj = bAlt && AltProjectileClass ? AltProjectileClass : ProjectileClass;
		if (bInstantHit || !Proj)
		{
			TraceFire(UseAccuracy, Pellets, bAlt);
		}
		else
		{
			SpawnProjectile(Proj, 0.f, bAlt);
		}
		if (AMFCharacter* C = GetOwnerCharacter())
		{
			C->MakeNoise(1.f, C);
		}
	}

	if (IsLocallyControlled() || GetNetMode() == NM_Standalone)
	{
		PlayFireEffects(bAlt);
	}
}

void AMFWeapon::TraceFire(float InAccuracy, int32 InPellets, bool bAlt)
{
	AMFCharacter* C = GetOwnerCharacter();
	if (!C)
	{
		return;
	}
	const FVector Start = GetFireStart();
	const FRotationMatrix Aim(GetAimRotation());
	const FVector X = Aim.GetUnitAxis(EAxis::X);
	const FVector Y = Aim.GetUnitAxis(EAxis::Y);
	const FVector Z = Aim.GetUnitAxis(EAxis::Z);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFWeaponTrace), true, C);
	Params.AddIgnoredActor(this);
	bImpactSoundSent = bFleshSoundSent = false;   // one of each per shot (pellets)
	Params.AddIgnoredActor(C->GetAttachParentActor());   // passengers shoot out of their vehicle

	for (int32 i = 0; i < FMath::Max(1, InPellets); ++i)
	{
		// MF TraceFire: End = Start + Acc*(rand-0.5)*Y*1000 + Acc*(rand-0.5)*Z*1000 + X*MaxTraceDistance
		const FVector End = Start
			+ InAccuracy * (FMath::FRand() - 0.5f) * Y * 2250.f
			+ InAccuracy * (FMath::FRand() - 0.5f) * Z * 2250.f
			+ X * MaxTraceDistance;
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_MFWeapon, Params))
		{
			ProcessTraceHit(Hit, (End - Start).GetSafeNormal(), bAlt);
		}
	}
}

void AMFWeapon::ProcessTraceHit(const FHitResult& Hit, const FVector& ShotDir, bool bAlt)
{
	AActor* Other = Hit.GetActor();
	if (!Other || Other == GetOwner())
	{
		return;
	}
	float Damage = HitDamage;
	TSubclassOf<UDamageType> Type = DamageType;
	float Momentum = HitMomentum;
	if (const AMFCharacter* Victim = Cast<AMFCharacter>(Other))
	{
		if (HeadshotDamage > 0.f && Victim->IsHeadshot(Hit.ImpactPoint))
		{
			Damage = HeadshotDamage;
			Type = HeadshotDamageType;
			Momentum *= 2.f;
		}
	}
	FPointDamageEvent Event(Damage, Hit, ShotDir, Type);
	APawn* Shooter = Cast<APawn>(GetOwner());
	Other->TakeDamage(Damage, Event, Shooter ? Shooter->GetController() : nullptr, this);
	if (Cast<AMFCharacter>(Other) ? !bFleshSoundSent : (!Cast<APawn>(Other) && !bImpactSoundSent))
	{
		(Cast<AMFCharacter>(Other) ? bFleshSoundSent : bImpactSoundSent) = true;
		MulticastImpactSound(Hit.ImpactPoint, Cast<AMFCharacter>(Other) != nullptr);
	}

	if (UPrimitiveComponent* Comp = Hit.GetComponent(); Comp && Comp->IsSimulatingPhysics())
	{
		Comp->AddImpulseAtLocation(ShotDir * Momentum, Hit.ImpactPoint);
	}
}

AMFProjectile* AMFWeapon::SpawnProjectile(TSubclassOf<AMFProjectile> Class, float Speed, bool bAlt)
{
	AMFCharacter* C = GetOwnerCharacter();
	if (!C || !Class)
	{
		return nullptr;
	}
	const FRotator Aim = GetAimRotation();
	const FVector Start = GetFireStart() + Aim.Vector() * 40.f;
	FActorSpawnParameters SP;
	SP.Owner = C;
	SP.Instigator = C;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMFProjectile* P = GetWorld()->SpawnActor<AMFProjectile>(Class, Start, Aim, SP);
	if (P)
	{
		P->Launch(Aim.Vector(), Speed, C->GetVelocity());
	}
	return P;
}

void AMFWeapon::PlayFireEffects(bool bAlt)
{
	PlaySoundLocal(FireSound);
	if (AMFCharacter* C = GetOwnerCharacter(); C && C->IsLocallyControlled())
	{
		C->AddRecoil(RecoilPower * 6.f);
	}
}

void AMFWeapon::OnRep_BurstCounter()
{
	PlayFireEffects(false);
}

void AMFWeapon::OnDepleted()
{
	if (AMFCharacter* C = GetOwnerCharacter())
	{
		C->RemoveWeapon(this, true);
		C->SwitchToBestWeapon();
	}
}

// --- equip / holster -----------------------------------------------------------------

void AMFWeapon::OnGiven(AMFCharacter* NewOwner)
{
	SetOwner(NewOwner);
	SetInstigator(NewOwner);
	if (NewOwner && NewOwner->GetWeaponAttachPoint())
	{
		AttachToComponent(NewOwner->GetWeaponAttachPoint(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
	ForceInactive();
}

void AMFWeapon::OnEquip()
{
	SetActorHiddenInGame(false);
	SetState(EMFWeaponState::Equipping, EquipTime);
	if (IsLocallyControlled())
	{
		PlaySoundLocal(SelectSound);
	}
}

void AMFWeapon::OnPutDown()
{
	bFireHeld = bAltFireHeld = false;
	if (ZoomLevel > 0)
	{
		ZoomLevel = 0;
		if (AMFCharacter* C = GetOwnerCharacter())
		{
			C->UpdateCarrySpeed();
		}
	}
	SetState(EMFWeaponState::PuttingDown, PutDownTime);
}

void AMFWeapon::DropFrom(const FVector& From, const FRotator& Facing)
{
	if (!HasAuthority())
	{
		return;
	}
	ForceInactive();
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetOwner(nullptr);
	SetInstigator(nullptr);
	// MF Pawn.DropItem: 4 collision radii (17 uu) out, yaw +-13000 of 65536 around the facing,
	// then PHYS_Falling until it lands
	const FRotator Dir(0.f, Facing.Yaw + FMath::FRandRange(-71.4f, 71.4f), 0.f);
	// world geometry only (vehicles too): soldiers, the dropper included, don't hold it up
	FCollisionQueryParams Params(TEXT("MFDrop"), false, this);
	const FCollisionObjectQueryParams World(ECC_TO_BITFIELD(ECC_WorldStatic) | ECC_TO_BITFIELD(ECC_WorldDynamic) | ECC_TO_BITFIELD(ECC_PhysicsBody));
	FHitResult Hit;
	FVector Spot = From + Dir.Vector() * 153.f;   // 4 x 17 uu
	if (GetWorld()->LineTraceSingleByObjectType(Hit, From, Spot, World, Params))
	{
		Spot = Hit.Location - Dir.Vector() * 20.f;
	}
	// lands with the model's underside on the floor; falls there from where it left the hand
	const float Bottom = Mesh1P->GetStaticMesh() ? Mesh1P->GetStaticMesh()->GetBoundingBox().Min.Z : -5.f;
	LandZ = Spot.Z - 5000.f;
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Spot, Spot - FVector(0, 0, 5000.f), World, Params))
	{
		LandZ = Hit.Location.Z - Bottom + 1.f;
	}
	FallSpeed = 0.f;
	SetActorLocationAndRotation(FVector(Spot.X, Spot.Y, FMath::Max(Spot.Z, LandZ)), Dir);
	SetActorTickEnabled(GetActorLocation().Z > LandZ);
	bDropped = true;
	DroppedTime = GetWorld()->GetTimeSeconds();
	SetReplicatingMovement(true);
	UpdateDroppedLook();
	ForceNetUpdate();

	// keep the world tidy: the oldest dropped weapon goes beyond the mode's MaxDroppedWeapons
	const AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>();
	const int32 MaxDropped = GM ? GM->MaxDroppedWeapons : 32;
	AMFWeapon* Oldest = nullptr;
	int32 Count = 0;
	for (TActorIterator<AMFWeapon> It(GetWorld()); It; ++It)
	{
		if (It->IsDropped())
		{
			++Count;
			if (!Oldest || It->DroppedTime < Oldest->DroppedTime)
			{
				Oldest = *It;
			}
		}
	}
	if (Count > MaxDropped && Oldest)
	{
		Oldest->Destroy();
	}
}

void AMFWeapon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FVector L = GetActorLocation();
	FallSpeed += -GetWorld()->GetGravityZ() * DeltaSeconds;
	L.Z -= FallSpeed * DeltaSeconds;
	if (!bDropped || L.Z <= LandZ)
	{
		L.Z = FMath::Max(L.Z, LandZ);
		SetActorTickEnabled(false);
		UE_LOG(LogMF, Verbose, TEXT("%s landed at %s"), *GetName(), *L.ToCompactString());
	}
	SetActorLocation(L);
}

void AMFWeapon::OnPickedUp(AMFCharacter* NewOwner)
{
	SetActorTickEnabled(false);
	bDropped = false;
	SetReplicatingMovement(false);
	UpdateDroppedLook();
	OnGiven(NewOwner);
	ForceNetUpdate();
}

void AMFWeapon::OnRep_Dropped()
{
	UpdateDroppedLook();
}

void AMFWeapon::UpdateDroppedLook()
{
	// model on the floor for everyone (full size); in hand only the owner sees it
	Mesh1P->SetOnlyOwnerSee(!bDropped);
	if (bHasViewMesh)
	{
		Mesh1P->SetRelativeTransform(bDropped ? FTransform::Identity : HandTransform);
	}
	if (bDropped)
	{
		SetActorHiddenInGame(false);
	}
	else if (State == EMFWeaponState::Inactive)
	{
		SetActorHiddenInGame(true);
	}
}

void AMFWeapon::ForceInactive()
{
	ClearStateTimer();
	State = EMFWeaponState::Inactive;
	bFireHeld = bAltFireHeld = false;
	bPendingReload = false;
	ZoomLevel = 0;
	SetActorHiddenInGame(true);
}

void AMFWeapon::MulticastImpactSound_Implementation(FVector_NetQuantize At, bool bFlesh)
{
	if (bFlesh)
	{
		// PlaySound(Sound'MiscSFX.RageChunkHit',, 4.0,,100): only right next to it
		MF::PlaySoundRadius(this, MF::LoadClassic<USoundBase>(TEXT("Sounds/MiscSFX/RageChunkHit")), At, 1.f, 100.f);
		return;
	}
	// RageEffect.AddWallHit: a ricochet, one of two impacts, or nothing (a quarter each)
	const float R = FMath::FRand();
	if (R < 0.25f)
	{
		MF::PlaySoundRadius(this, MF::LoadClassic<USoundBase>(TEXT("Sounds/WallHitSFX/Ricochet")), At, 0.6f, 1200.f, 0.5f + FMath::FRand());
	}
	else if (R < 0.75f)
	{
		MF::PlaySoundRadius(this, MF::LoadClassic<USoundBase>(R < 0.5f ? TEXT("Sounds/WallHitSFX/imp01") : TEXT("Sounds/WallHitSFX/imp02")), At, 0.9f, 1000.f);
	}
}
