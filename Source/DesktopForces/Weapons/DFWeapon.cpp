#include "Weapons/DFWeapon.h"
#include "Player/DFSoldierAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Weapons/DFTracer.h"
#include "Weapons/DFShellCase.h"
#include "World/DFDecals.h"
#include "Sound/SoundWave.h"
#include "Components/AudioComponent.h"
#include "Game/DFGameMode.h"
#include "Core/DFProps.h"
#include "Core/DFContent.h"
#include "Weapons/DFProjectile.h"
#include "Player/DFCharacter.h"
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

ADFWeapon::ADFWeapon()
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

	DamageType = UDFDamage_Bullet::StaticClass();
	HeadshotDamageType = UDFDamage_Headshot::StaticClass();
	// MF's shared icon sheet (converted by Tools/mfport); each weapon sets its rect
	HUDIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/MobileForcesClassic/Textures/RageWeapons/WeaponIcons.WeaponIcons")));
	SetActorHiddenInGame(true);
}

void ADFWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ADFWeapon, ClipAmmo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ADFWeapon, TotalAmmo, COND_OwnerOnly);
	DOREPLIFETIME(ADFWeapon, ZoomLevel);
	DOREPLIFETIME(ADFWeapon, bDropped);
	DOREPLIFETIME_CONDITION(ADFWeapon, BurstCounter, COND_SkipOwner);
}

void ADFWeapon::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (HasAuthority())
	{
		GiveFullAmmo();   // RageWeapon.PostBeginPlay
	}
	ResolveSounds();
	if (!ViewMesh.IsEmpty() && DF::ApplyPropMesh(Mesh1P, *ViewMesh))
	{
		// view-model placement from the mesh's bounds: rear ViewOffset.X in front of the eye,
		// centred ViewOffset.Y to the right, top ViewOffset.Z below, at ViewScale
		// (the bounds as turned by ViewRotation: some mods' models face the other way)
		const FBox B = Mesh1P->GetStaticMesh()->GetBoundingBox().TransformBy(FTransform(ViewRotation));
		const FVector Loc = ViewOffset + FVector(-B.Min.X, -B.GetCenter().Y, -B.Max.Z) * ViewScale;
		HandTransform = FTransform(ViewRotation, Loc, FVector(ViewScale));
		bHasViewMesh = true;
		if (GetNetMode() != NM_DedicatedServer)
		{
			Mesh3P = NewObject<UStaticMeshComponent>(this, TEXT("Mesh3P"));
			Mesh3P->SetStaticMesh(Mesh1P->GetStaticMesh());
			for (int32 i = 0; i < Mesh1P->GetNumMaterials(); ++i)
			{
				Mesh3P->SetMaterial(i, Mesh1P->GetMaterial(i));
			}
			Mesh3P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh3P->SetOwnerNoSee(true);
			Mesh3P->bCastDynamicShadow = false;
			Mesh3P->SetGenerateOverlapEvents(false);
			Mesh3P->SetVisibility(false);   // until it is in a soldier's hand
			Mesh3P->SetupAttachment(RootComponent);
			Mesh3P->RegisterComponent();
		}
		UpdateDroppedLook();
	}
}

void ADFWeapon::Destroyed()
{
	ClearStateTimer();
	SetAmbientMFSound(nullptr);
	Super::Destroyed();
}

bool ADFWeapon::IsContentAvailable() const
{
	return RequiredContent.IsEmpty() || DF::LoadClassicObject(UObject::StaticClass(), RequiredContent) != nullptr;
}

void ADFWeapon::ResolveSounds()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// converted sounds: <content root>/Sounds/<Package>/<Name> (Tools/mfport; DF::ContentRoots)
	auto Load = [](TObjectPtr<USoundBase>& Slot, const FString& ClassicName)
	{
		FString Package, Name;
		if (!Slot && ClassicName.Split(TEXT("."), &Package, &Name))
		{
			Slot = DF::LoadClassic<USoundBase>(FString::Printf(TEXT("Sounds/%s/%s"), *Package, *Name));
		}
	};
	Load(FireSound, ClassicFireSound);
	Load(ReloadSound, ClassicReloadSound);
	Load(SelectSound, ClassicSelectSound);
}

// --- ammo ----------------------------------------------------------------------------

void ADFWeapon::GiveFullAmmo()
{
	ClipAmmo = MaxClipAmmo;
	TotalAmmo = MaxClips * MaxClipAmmo - MaxClipAmmo;
}

void ADFWeapon::ReloadAmmo()
{
	const int32 ToFill = FMath::Min(MaxClipAmmo - ClipAmmo, TotalAmmo);
	if (ToFill > 0)
	{
		TotalAmmo -= ToFill;
		ClipAmmo += ToFill;
	}
}

// --- helpers -------------------------------------------------------------------------

ADFCharacter* ADFWeapon::GetOwnerCharacter() const
{
	return Cast<ADFCharacter>(GetOwner());
}

bool ADFWeapon::IsLocallyControlled() const
{
	const APawn* P = Cast<APawn>(GetOwner());
	return P && P->IsLocallyControlled();
}

bool ADFWeapon::IsSimulatingLocally() const
{
	return HasAuthority() || IsLocallyControlled();
}

FVector ADFWeapon::GetFireStart() const
{
	const ADFCharacter* C = GetOwnerCharacter();
	return C ? C->GetPawnViewLocation() : GetActorLocation();
}

FRotator ADFWeapon::GetAimRotation() const
{
	const ADFCharacter* C = GetOwnerCharacter();
	return C ? C->GetBaseAimRotation() : GetActorRotation();
}

float ADFWeapon::GetFOVScale() const
{
	return ZoomLevel > 0 ? 1.f / FMath::Max(ZoomScale, 1.f) : 1.f;
}

float ADFWeapon::GetMoveSpeedScale() const
{
	return ZoomLevel > 0 ? ZoomedMoveScale : 1.f;
}

FString ADFWeapon::GetHUDStatus() const
{
	return FString();
}

void ADFWeapon::PlayMFSound(const TCHAR* MFName) const
{
	FString Package, Name;
	if (GetNetMode() != NM_DedicatedServer && FString(MFName).Split(TEXT("."), &Package, &Name))
	{
		PlaySoundLocal(DF::LoadClassic<USoundBase>(FString::Printf(TEXT("Sounds/%s/%s"), *Package, *Name)));
	}
}

void ADFWeapon::SetAmbientMFSound(const TCHAR* MFName)
{
	if (AmbientAudio)
	{
		AmbientAudio->Stop();
		AmbientAudio = nullptr;
	}
	FString Package, Name;
	if (!MFName || GetNetMode() == NM_DedicatedServer || !FString(MFName).Split(TEXT("."), &Package, &Name))
	{
		return;
	}
	USoundBase* Sound = DF::LoadClassic<USoundBase>(FString::Printf(TEXT("Sounds/%s/%s"), *Package, *Name));
	if (USoundWave* Wave = Cast<USoundWave>(Sound))
	{
		Wave->bLooping = true;   // UE1 AmbientSound loops while it's set
	}
	const ADFCharacter* C = GetOwnerCharacter();
	if (Sound && C)
	{
		AmbientAudio = UGameplayStatics::SpawnSoundAttached(Sound, C->GetRootComponent(), NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
			true, 1.f, 1.f, 0.f, DF::GetAttenuation(1600.f));
	}
}

void ADFWeapon::PlaySoundLocal(USoundBase* Sound) const
{
	if (Sound && GetNetMode() != NM_DedicatedServer)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, GetFireStart());
	}
}

// --- state machine -------------------------------------------------------------------

void ADFWeapon::SetState(EDFWeaponState NewState, float Duration)
{
	State = NewState;
	ClearStateTimer();
	if (Duration > 0.f)
	{
		GetWorldTimerManager().SetTimer(StateTimer, this, &ADFWeapon::OnStateTimer, Duration, false);
	}
	else if (NewState != EDFWeaponState::Idle && NewState != EDFWeaponState::Inactive)
	{
		// no animation (MF items with empty Play*Anim): done on the next frame
		StateTimer = GetWorldTimerManager().SetTimerForNextTick(this, &ADFWeapon::OnStateTimer);
	}
}

void ADFWeapon::ClearStateTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StateTimer);
	}
}

void ADFWeapon::OnStateTimer()
{
	switch (State)
	{
	case EDFWeaponState::Equipping:
		State = EDFWeaponState::Idle;
		Finish();
		break;
	case EDFWeaponState::Firing:
	case EDFWeaponState::Zooming:
		Finish();
		break;
	case EDFWeaponState::Reloading:
		ReloadAmmo();
		Finish();
		break;
	case EDFWeaponState::PuttingDown:
		ForceInactive();
		if (ADFCharacter* C = GetOwnerCharacter())
		{
			C->OnWeaponPutDown(this);
		}
		break;
	default:
		break;
	}
}

void ADFWeapon::Finish()
{
	ADFCharacter* C = GetOwnerCharacter();
	State = EDFWeaponState::Idle;
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

void ADFWeapon::StartFire(bool bAlt)
{
	(bAlt ? bAltFireHeld : bFireHeld) = true;
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerStartFire(bAlt);
	}
	if (!IsSimulatingLocally() || State != EDFWeaponState::Idle)
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

void ADFWeapon::StopFire(bool bAlt)
{
	(bAlt ? bAltFireHeld : bFireHeld) = false;
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerStopFire(bAlt);
	}
}

void ADFWeapon::ServerStartFire_Implementation(bool bAlt)
{
	StartFire(bAlt);
}

void ADFWeapon::ServerStopFire_Implementation(bool bAlt)
{
	StopFire(bAlt);
}

void ADFWeapon::StartReload()
{
	if (GetLocalRole() < ROLE_Authority && IsLocallyControlled() && State == EDFWeaponState::Idle)
	{
		ServerReload();
	}
	if (!IsSimulatingLocally() || !CanReload())
	{
		return;
	}
	if (State != EDFWeaponState::Idle)
	{
		return;
	}
	if (ZoomLevel > 0)
	{
		ZoomLevel = 0;
		if (ADFCharacter* C = GetOwnerCharacter())
		{
			C->UpdateCarrySpeed();
		}
	}
	SetState(EDFWeaponState::Reloading, ReloadTime);
	if (IsLocallyControlled())
	{
		PlaySoundLocal(ReloadSound);
	}
}

void ADFWeapon::ServerReload_Implementation()
{
	StartReload();
}

bool ADFWeapon::TryFire(bool bAlt)
{
	ADFCharacter* C = GetOwnerCharacter();
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
	SetState(EDFWeaponState::Firing, Duration);
	FireShot(bAlt);
	return true;
}

void ADFWeapon::ToggleZoom()
{
	ZoomLevel = (ZoomLevel + 1) % (MaxZoomLevel + 1);
	SetState(EDFWeaponState::Zooming, ZoomTime);
	if (ADFCharacter* C = GetOwnerCharacter())
	{
		C->UpdateCarrySpeed();
	}
	if (IsLocallyControlled())
	{
		PlaySoundLocal(ZoomLevel > 0 ? ZoomInSound : ZoomOutSound);
	}
}

void ADFWeapon::OnRep_ZoomLevel(uint8 OldZoom)
{
	if (ADFCharacter* C = GetOwnerCharacter())
	{
		C->UpdateCarrySpeed();
	}
}

// --- firing --------------------------------------------------------------------------

void ADFWeapon::FireShot(bool bAlt)
{
	ClipAmmo = FMath::Max(0, ClipAmmo - 1);
	bLastShotAlt = bAlt;

	if (HasAuthority())
	{
		++BurstCounter;
		const bool bZoomed = ZoomLevel > 0;
		const float UseAccuracy = (bAlt || bZoomed) ? AltAccuracy : Accuracy;
		TSubclassOf<ADFProjectile> Proj = bAlt && AltProjectileClass ? AltProjectileClass : ProjectileClass;
		if (bInstantHit || !Proj)
		{
			TraceFire(UseAccuracy, Pellets, bAlt);
		}
		else
		{
			SpawnProjectile(Proj, 0.f, bAlt);
		}
		if (ADFCharacter* C = GetOwnerCharacter())
		{
			C->MakeNoise(1.f, C);
		}
	}

	if (IsLocallyControlled() || GetNetMode() == NM_Standalone)
	{
		PlayFireEffects(bAlt);
	}
}

void ADFWeapon::TraceFire(float InAccuracy, int32 InPellets, bool bAlt)
{
	ADFCharacter* C = GetOwnerCharacter();
	if (!C)
	{
		return;
	}
	const FVector Start = GetFireStart();
	const FRotationMatrix Aim(GetAimRotation());
	const FVector X = Aim.GetUnitAxis(EAxis::X);
	const FVector Y = Aim.GetUnitAxis(EAxis::Y);
	const FVector Z = Aim.GetUnitAxis(EAxis::Z);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFWeaponTrace), true, C);
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
		const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_MFWeapon, Params);
		if (bHit)
		{
			ProcessTraceHit(Hit, (End - Start).GetSafeNormal(), bAlt);
		}
		// RageWeapon.TraceFire: TracerCount counts down, a tracer when it runs out
		if (TracerEvery > 0 && --TracerCount <= 0)
		{
			TracerCount = TracerEvery;
			const FVector To = bHit ? FVector(Hit.ImpactPoint) : End;
			if (IsLocallyControlled())
			{
				ADFTracer::Fire(GetWorld(), Start, To);
			}
			if (HasAuthority())
			{
				MulticastTracer(Start, To);
			}
		}
	}
}

void ADFWeapon::MulticastTracer_Implementation(FVector_NetQuantize Start, FVector_NetQuantize End)
{
	if (!IsLocallyControlled())
	{
		ADFTracer::Fire(GetWorld(), Start, End);
	}
}

void ADFWeapon::ProcessTraceHit(const FHitResult& Hit, const FVector& ShotDir, bool bAlt)
{
	AActor* Other = Hit.GetActor();
	if (!Other || Other == GetOwner())
	{
		return;
	}
	float Damage = HitDamage;
	TSubclassOf<UDamageType> Type = DamageType;
	float Momentum = HitMomentum;
	if (const ADFCharacter* Victim = Cast<ADFCharacter>(Other))
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
	if (Cast<ADFCharacter>(Other) ? !bFleshSoundSent : (bWallImpactFX && !Cast<APawn>(Other) && !bImpactSoundSent))
	{
		(Cast<ADFCharacter>(Other) ? bFleshSoundSent : bImpactSoundSent) = true;
		MulticastImpactFX(Hit.ImpactPoint, Hit.ImpactNormal, Cast<ADFCharacter>(Other) != nullptr);
	}

	if (UPrimitiveComponent* Comp = Hit.GetComponent(); Comp && Comp->IsSimulatingPhysics())
	{
		Comp->AddImpulseAtLocation(ShotDir * Momentum, Hit.ImpactPoint);
	}
}

ADFProjectile* ADFWeapon::SpawnProjectile(TSubclassOf<ADFProjectile> Class, float Speed, bool bAlt)
{
	ADFCharacter* C = GetOwnerCharacter();
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
	ADFProjectile* P = GetWorld()->SpawnActor<ADFProjectile>(Class, Start, Aim, SP);
	if (P)
	{
		P->Launch(Aim.Vector(), Speed, C->GetVelocity());
	}
	return P;
}

void ADFWeapon::PlayFireEffects(bool bAlt)
{
	PlaySoundLocal(FireSound);
	ADFCharacter* C = GetOwnerCharacter();
	if (C && C->IsLocallyControlled())
	{
		C->AddRecoil(RecoilPower * 6.f);
	}
	if (C && C->IsLocallyControlled() && MuzzleFlashSize > 0.f && GetNetMode() != NM_DedicatedServer)
	{
		ShowMuzzleFlash();
	}
	if (UDFSoldierAnimInstance* Anim = C ? C->GetSoldierAnim() : nullptr)
	{
		Anim->OnFire(bAlt);   // knife stab, grenade / trip bomb throw
	}
	if (C && !CasingMesh.IsEmpty())
	{
		// EjectBrass, from where the shooter looks
		FVector Eye;
		FRotator View;
		C->GetActorEyesViewPoint(Eye, View);
		ADFShellCase::Eject(GetWorld(), CasingMesh, CasingScale, Eye, View, C->GetVelocity());
	}
}

void ADFWeapon::ShowMuzzleFlash()
{
	if (!bHasViewMesh || !Mesh1P->GetStaticMesh())
	{
		return;
	}
	if (!MuzzleFlash)
	{
		MuzzleFlash = NewObject<UStaticMeshComponent>(this);
		MuzzleFlash->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
		MuzzleFlash->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MF/Materials/M_MF_MuzzleFlash.M_MF_MuzzleFlash"), nullptr, LOAD_NoWarn | LOAD_Quiet));
		MuzzleFlash->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MuzzleFlash->SetCastShadow(false);
		MuzzleFlash->SetOnlyOwnerSee(true);
		MuzzleFlash->SetupAttachment(Mesh1P);
		MuzzleFlash->RegisterComponent();
		// at the front of the model, a little below its top (where barrels are), facing back at the eye
		const FBox B = Mesh1P->GetStaticMesh()->GetBoundingBox();
		MuzzleFlash->SetRelativeLocation(FVector(B.Max.X + 2.f, B.GetCenter().Y, B.Max.Z - B.GetSize().Z * 0.2f));
	}
	// a random turn and frame-ish size each shot (FlashYellow A01-A04 in MF)
	MuzzleFlash->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));   // the plane's face towards the eye
	MuzzleFlash->AddLocalRotation(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f));
	const float Size = MuzzleFlashSize * FMath::FRandRange(0.8f, 1.2f) / FMath::Max(Mesh1P->GetRelativeScale3D().X, 0.01f) / 100.f;
	MuzzleFlash->SetRelativeScale3D(FVector(Size));
	MuzzleFlash->SetVisibility(true);
	GetWorldTimerManager().SetTimer(MuzzleFlashTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { if (MuzzleFlash) { MuzzleFlash->SetVisibility(false); } }), 0.05f, false);
}

void ADFWeapon::OnRep_BurstCounter()
{
	PlayFireEffects(false);
}

void ADFWeapon::OnDepleted()
{
	if (ADFCharacter* C = GetOwnerCharacter())
	{
		C->RemoveWeapon(this, true);
		C->SwitchToBestWeapon();
	}
}

// --- equip / holster -----------------------------------------------------------------

void ADFWeapon::OnGiven(ADFCharacter* NewOwner)
{
	SetOwner(NewOwner);
	SetInstigator(NewOwner);
	if (NewOwner && NewOwner->GetWeaponAttachPoint())
	{
		AttachToComponent(NewOwner->GetWeaponAttachPoint(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
	ForceInactive();
}

void ADFWeapon::OnEquip()
{
	SetActorHiddenInGame(false);
	SetState(EDFWeaponState::Equipping, EquipTime);
	if (ADFCharacter* C = GetOwnerCharacter(); C && C->GetSoldierAnim())
	{
		AttachToSoldier(C->GetMesh());
	}
	if (IsLocallyControlled())
	{
		PlaySoundLocal(SelectSound);
	}
}

void ADFWeapon::OnPutDown()
{
	bFireHeld = bAltFireHeld = false;
	SetAmbientMFSound(nullptr);
	if (ZoomLevel > 0)
	{
		ZoomLevel = 0;
		if (ADFCharacter* C = GetOwnerCharacter())
		{
			C->UpdateCarrySpeed();
		}
	}
	SetState(EDFWeaponState::PuttingDown, PutDownTime);
}

void ADFWeapon::DropFrom(const FVector& From, const FRotator& Facing)
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
	FCollisionQueryParams Params(TEXT("DFDrop"), false, this);
	const FCollisionObjectQueryParams World(ECC_TO_BITFIELD(ECC_WorldStatic) | ECC_TO_BITFIELD(ECC_WorldDynamic) | ECC_TO_BITFIELD(ECC_PhysicsBody));
	FHitResult Hit;
	FVector Spot = From + Dir.Vector() * 153.f;   // 4 x 17 uu
	if (GetWorld()->LineTraceSingleByObjectType(Hit, From, Spot, World, Params))
	{
		Spot = Hit.Location - Dir.Vector() * 20.f;
	}
	PlaceAsPickup(Spot, Dir);

	// keep the world tidy: the oldest dropped weapon goes beyond the mode's MaxDroppedWeapons
	// (items the map placed don't count)
	const ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>();
	const int32 MaxDropped = GM ? GM->MaxDroppedWeapons : 32;
	ADFWeapon* Oldest = nullptr;
	int32 Count = 0;
	for (TActorIterator<ADFWeapon> It(GetWorld()); It; ++It)
	{
		if (It->IsDropped() && !It->bMapPickup)
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

void ADFWeapon::PlaceAsPickup(const FVector& Spot, const FRotator& Facing)
{
	// lands with the model's underside on the floor; falls there from Spot
	FCollisionQueryParams Params(TEXT("DFDrop"), false, this);
	const FCollisionObjectQueryParams World(ECC_TO_BITFIELD(ECC_WorldStatic) | ECC_TO_BITFIELD(ECC_WorldDynamic) | ECC_TO_BITFIELD(ECC_PhysicsBody));
	const float Bottom = Mesh1P->GetStaticMesh() ? Mesh1P->GetStaticMesh()->GetBoundingBox().Min.Z : -5.f;
	FHitResult Hit;
	LandZ = Spot.Z - 5000.f;
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Spot, Spot - FVector(0, 0, 5000.f), World, Params))
	{
		LandZ = Hit.Location.Z - Bottom + 1.f;
	}
	FallSpeed = 0.f;
	SetActorLocationAndRotation(FVector(Spot.X, Spot.Y, FMath::Max(Spot.Z, LandZ)), Facing);
	SetActorTickEnabled(GetActorLocation().Z > LandZ);
	bDropped = true;
	DroppedTime = GetWorld()->GetTimeSeconds();
	SetReplicatingMovement(true);
	UpdateDroppedLook();
	ForceNetUpdate();
}

void ADFWeapon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FVector L = GetActorLocation();
	FallSpeed += -GetWorld()->GetGravityZ() * DeltaSeconds;
	L.Z -= FallSpeed * DeltaSeconds;
	if (!bDropped || L.Z <= LandZ)
	{
		L.Z = FMath::Max(L.Z, LandZ);
		SetActorTickEnabled(false);
		UE_LOG(LogDF, Verbose, TEXT("%s landed at %s"), *GetName(), *L.ToCompactString());
	}
	SetActorLocation(L);
}

void ADFWeapon::OnPickedUp(ADFCharacter* NewOwner)
{
	SetActorTickEnabled(false);
	bDropped = false;
	bMapPickup = false;
	SetReplicatingMovement(false);
	UpdateDroppedLook();
	OnGiven(NewOwner);
	ForceNetUpdate();
}

void ADFWeapon::OnRep_Dropped()
{
	UpdateDroppedLook();
}

void ADFWeapon::AttachToSoldier(USkeletalMeshComponent* Body)
{
	if (!Mesh3P || !Body || bDropped)
	{
		return;
	}
	if (Mesh3P->GetAttachParent() != Body)
	{
		// MESH WEAPONATTACH BONE="Bone_Weapon", WEAPONPOSITION YAW=64 PITCH=-64 (256ths of a turn)
		Mesh3P->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("Bone_weapon"));
		Mesh3P->SetRelativeRotation(FRotator(-90.f, 90.f, 0.f));
	}
	Mesh3P->SetVisibility(true);
}

void ADFWeapon::UpdateDroppedLook()
{
	if (Mesh3P && bDropped)
	{
		Mesh3P->SetVisibility(false);
		Mesh3P->AttachToComponent(RootComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
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
	else if (State == EDFWeaponState::Inactive)
	{
		SetActorHiddenInGame(true);
	}
}

void ADFWeapon::ForceInactive()
{
	ClearStateTimer();
	State = EDFWeaponState::Inactive;
	bFireHeld = bAltFireHeld = false;
	bPendingReload = false;
	ZoomLevel = 0;
	SetActorHiddenInGame(true);
}

void ADFWeapon::MulticastImpactFX_Implementation(FVector_NetQuantize At, FVector_NetQuantizeNormal Normal, bool bFlesh)
{
	if (bFlesh)
	{
		// PlaySound(Sound'MiscSFX.RageChunkHit',, 4.0,,100): only right next to it; RageEffect.AddBlood
		DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/MiscSFX/RageChunkHit")), At, 1.f, 100.f);
		DF::SpawnEffect(this, TEXT("Blood"), At);
		return;
	}
	// RageEffect.AddWallHit: windy smoke and specks off the wall (+X of the effect: the normal),
	// a hole (RagePock: BulletHole01-04 at DrawScale 0.15, ~11 cm)
	DF::SpawnEffect(this, TEXT("WallHit"), At, 1.f, FVector(Normal).Rotation());
	DF::SpawnDecal(this, DF::EDecal::BulletHole, At, Normal, 12.f);
	// RageEffect.AddWallHit: a ricochet, one of two impacts, or nothing (a quarter each)
	const float R = FMath::FRand();
	if (R < 0.25f)
	{
		DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/WallHitSFX/Ricochet")), At, 0.6f, 1200.f, 0.5f + FMath::FRand());
	}
	else if (R < 0.75f)
	{
		DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(R < 0.5f ? TEXT("Sounds/WallHitSFX/imp01") : TEXT("Sounds/WallHitSFX/imp02")), At, 0.9f, 1000.f);
	}
}
