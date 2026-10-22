#include "Player/DFCharacter.h"
#include "Game/DFGameState.h"
#include "Core/DFProps.h"
#include "Player/DFPlayerController.h"
#include "Sound/SoundBase.h"
#include "Core/DFContent.h"
#include "World/DFZoneVolume.h"
#include "Vehicles/DFVehicle.h"
#include "Core/DFUserSettings.h"
#include "Player/DFCharacterMovement.h"
#include "Player/DFSoldierAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Weapons/DFWeapon.h"
#include "Weapons/DFWeapons.h"
#include "Game/DFGameMode.h"
#include "Game/DFPlayerState.h"
#include "UI/DFHUD.h"
#include "DesktopForces.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"
#include "Scalability.h"
#include "UObject/ConstructorHelpers.h"

ADFCharacter::ADFCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UDFCharacterMovement>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	GetCapsuleComponent()->InitCapsuleSize(38.25f, 87.75f);   // MF CollisionRadius 17, CollisionHeight 39
	BaseEyeHeight = 60.75f;                                    // MF BaseEyeHeight 27
	CrouchedEyeHeight = 30.f;
	GetCharacterMovement()->SetCrouchedHalfHeight(45.f);       // MF CrouchHeight 20

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, BaseEyeHeight));
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->SetFieldOfView(DefaultFOV);
	FirstPersonCamera->PostProcessBlendWeight = 0.f;

	WeaponAttach = CreateDefaultSubobject<USceneComponent>(TEXT("WeaponAttach"));
	WeaponAttach->SetupAttachment(FirstPersonCamera);

	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(GetCapsuleComponent());
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetOwnerNoSee(true);
	PlaceholderBody->bCastDynamicShadow = false;
	PlaceholderBody->PrimaryComponentTick.bCanEverTick = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(Cylinder.Object);
		PlaceholderBody->SetRelativeScale3D(FVector(0.7f, 0.7f, 1.75f));
	}

	// The skeletal mesh slot stays empty until the MF soldier meshes are converted.
	GetMesh()->SetVisibility(false);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	DefaultInventory.Add(AKnife::StaticClass());

	SetNetUpdateFrequency(30.f);
	SetMinNetUpdateFrequency(10.f);
}

UDFCharacterMovement* ADFCharacter::GetMFMovement() const
{
	return Cast<UDFCharacterMovement>(GetCharacterMovement());
}

void ADFCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADFCharacter, Health);
	DOREPLIFETIME_CONDITION(ADFCharacter, bParachute, COND_SkipOwner);
	DOREPLIFETIME(ADFCharacter, MaxHealth);
	DOREPLIFETIME_CONDITION(ADFCharacter, bInfiniteAmmo, COND_OwnerOnly);
	DOREPLIFETIME(ADFCharacter, MaxCarry);
	DOREPLIFETIME(ADFCharacter, BodyScale);
	DOREPLIFETIME(ADFCharacter, bDying);
	DOREPLIFETIME(ADFCharacter, Inventory);
	DOREPLIFETIME(ADFCharacter, CurrentWeapon);
	DOREPLIFETIME(ADFCharacter, bInLoadout);
}

void ADFCharacter::SetInfiniteAmmo(bool bOn)
{
	bInfiniteAmmo = bOn;
	for (ADFWeapon* W : Inventory)
	{
		if (W && W->HasInfiniteAmmo())
		{
			W->GiveFullAmmo();   // ZombiePlayer.AddInventory: MaxClipAmmo 9999, GiveFullAmmo
		}
	}
}

void ADFCharacter::BeginPlay()
{
	Super::BeginPlay();
	Health = MaxHealth;
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(BreathTimer, this, &ADFCharacter::BreathTick, 1.f, true);
	}
	FirstPersonCamera->SetFieldOfView(DefaultFOV);
	if (IsLocallyControlled() && Scalability::GetQualityLevels().PostProcessQuality >= 3)
	{
		// Epic settings: a little bloom; every lower level stays post-process free
		FirstPersonCamera->PostProcessBlendWeight = 1.f;
		FirstPersonCamera->PostProcessSettings.bOverride_BloomIntensity = true;
		FirstPersonCamera->PostProcessSettings.BloomIntensity = 0.4f;
	}
	if (!IsLocallyControlled())
	{
		SetActorTickEnabled(false);   // only the local player needs per-frame camera work
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		GetWorldTimerManager().SetTimer(SoldierTimer, this, &ADFCharacter::UpdateSoldier, 0.5f, true, 0.f);
	}
}

UDFSoldierAnimInstance* ADFCharacter::GetSoldierAnim() const
{
	return SoldierModel >= 0 ? Cast<UDFSoldierAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
}

void ADFCharacter::UpdateSoldier()
{
	const ADFPlayerState* PS = GetPlayerState<ADFPlayerState>();
	if (!PS || !IsAlive())
	{
		return;
	}
	// MF: the helmet soldier (RagePlayerHelmet: RagePlayer1Mesh, MFTeamA skins) or the
	// balaclava one (RagePlayerBalaclava: RagePlayer2Mesh, MFTeamB); every player gets one of
	// the two until player setup offers the choice
	const int32 Model = PS->GetPlayerId() & 1;
	USkeletalMeshComponent* Body = GetMesh();
	if (Model != SoldierModel)
	{
		const FString Name = FString::Printf(TEXT("RagePlayer%dMesh"), Model + 1);
		USkeletalMesh* Mesh = DF::LoadClassic<USkeletalMesh>(FString::Printf(TEXT("Soldiers/%s/%s/SkeletalMeshes/%s"), *Name, *Name, *Name));
		if (!Mesh)
		{
			GetWorldTimerManager().ClearTimer(SoldierTimer);   // content without the soldiers: the placeholder stays
			return;
		}
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // hits stay on the capsule
		Body->bCastDynamicShadow = false;
		Body->bEnableUpdateRateOptimizations = true;
		Body->SetAnimInstanceClass(UDFSoldierAnimInstance::StaticClass());
		Body->SetSkeletalMesh(Mesh);
		Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, SoldierZOffset), FRotator::ZeroRotator);
		if (UDFSoldierAnimInstance* Anim = Cast<UDFSoldierAnimInstance>(Body->GetAnimInstance()))
		{
			// the importer names every sequence after the mesh: RagePlayer1MeshKnifeStillSm
			Anim->SetSequencePrefix(Mesh->GetOutermost()->GetName());
		}
		Body->SetVisibility(true);
		PlaceholderBody->SetVisibility(false);
		SoldierModel = Model;
		SoldierSkin = -1;
	}
	// Pawn.SetMultiSkin: skin elements 0-3 are <skin>00-03 (blue, and everyone without a
	// team) or 04-07 (red); a captain's TeamSkinCaptain element is <skin>08 / 09
	const ADFGameState* GS = GetWorld()->GetGameState<ADFGameState>();
	const uint8 Team = GS && GS->IsTeamGame() ? GetTeam() : DF::NoTeam;
	const bool bCaptain = PS->bCaptain && Team <= 1;
	const int32 Skin = (Team <= 1 ? Team : 0) * 2 + (bCaptain ? 1 : 0);
	if (Skin != SoldierSkin)
	{
		const TCHAR Set = Model ? TEXT('B') : TEXT('A');
		const int32 CaptainElement = Model ? 2 : 0;   // TeamSkinCaptain: balaclava 2, helmet default 0
		for (int32 i = 0; i < 4; ++i)
		{
			const int32 Tex = bCaptain && i == CaptainElement ? 8 + Team : (Team == 1 ? 4 : 0) + i;
			if (UMaterialInterface* M = DF::LoadClassic<UMaterialInterface>(FString::Printf(TEXT("Materials/RagePlayerGfx/MI_MFTeam%c%02d_opaque"), Set, Tex)))
			{
				Body->SetMaterial(i, M);
			}
		}
		SoldierSkin = Skin;
	}
	if (CurrentWeapon)
	{
		CurrentWeapon->AttachToSoldier(Body);
	}
}

void ADFCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	SetActorTickEnabled(IsLocallyControlled());
	ResumeWeapon();
}

void ADFCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	SetActorTickEnabled(IsLocallyControlled());   // owning client: camera FOV + recoil
	ApplyUserSettings();
	ResumeWeapon();
}

void ADFCharacter::ResumeWeapon()
{
	// back from a vehicle's driving seat (ADFVehicle::TryEnter put the weapon away): bring it up
	if (CurrentWeapon && IsAlive() && CurrentWeapon->GetState() == EDFWeaponState::Inactive && !PendingWeapon)
	{
		CurrentWeapon->OnEquip();
	}
}

void ADFCharacter::ApplyUserSettings()
{
	if (IsLocallyControlled())
	{
		DefaultFOV = FMath::Clamp(UDFUserSettings::Get().FieldOfView, 60.f, 140.f);
		FirstPersonCamera->SetFieldOfView(GetDesiredFOV());
	}
}

void ADFCharacter::Destroyed()
{
	if (HasAuthority())
	{
		ClearInventory(false);
	}
	Super::Destroyed();
}

// --- team / camera -----------------------------------------------------------------

uint8 ADFCharacter::GetTeam() const
{
	const ADFPlayerState* PS = GetPlayerState<ADFPlayerState>();
	if (!PS)
	{
		// riding in a vehicle: the controller (and its player state) is on the vehicle
		if (const APawn* Ride = Cast<APawn>(GetAttachParentActor()))
		{
			PS = Ride->GetPlayerState<ADFPlayerState>();
		}
	}
	return PS ? PS->GetTeam() : DF::NoTeam;
}

float ADFCharacter::GetDesiredFOV() const
{
	return DefaultFOV * (CurrentWeapon ? CurrentWeapon->GetFOVScale() : 1.f);
}

void ADFCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsLocallyControlled())
	{
		// behind view: our own body shows, the held model (in front of the eyes) doesn't
		const bool bBehind = IsBehindView();
		PlaceholderBody->SetOwnerNoSee(!bBehind);
		GetMesh()->SetOwnerNoSee(!bBehind);
		if (CurrentWeapon)
		{
			CurrentWeapon->UpdateFirstPersonView(bBehind);
		}
		UpdateViewBob(DeltaSeconds);
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateFootsteps(DeltaSeconds);
	}

	// Zoom: MF interpolates the FOV over ZoomInTime (~0.35 s)
	const float Target = GetDesiredFOV();
	const float FOV = FirstPersonCamera->FieldOfView;
	if (!FMath::IsNearlyEqual(FOV, Target, 0.05f))
	{
		FirstPersonCamera->SetFieldOfView(FMath::FInterpTo(FOV, Target, DeltaSeconds, 12.f));
	}

	// Recoil: MF ViewRecoil ran once per frame at ~30 fps: pitch up and random yaw by
	// Recoil*20 rotation units, then halves. Stepped at 30 Hz to stay frame-rate independent.
	if (CurrentRecoil > 0.f && Controller)
	{
		RecoilAccumulator += DeltaSeconds;
		FRotator Rot = Controller->GetControlRotation();
		while (RecoilAccumulator >= 1.f / 30.f && CurrentRecoil > 0.f)
		{
			RecoilAccumulator -= 1.f / 30.f;
			const float Kick = CurrentRecoil * 20.f * (360.f / 65536.f);
			Rot.Pitch += Kick;
			Rot.Yaw += FMath::RandBool() ? Kick : -Kick;
			CurrentRecoil *= 0.5f;
			if (CurrentRecoil <= 0.01f)
			{
				CurrentRecoil = 0.f;
			}
		}
		Controller->SetControlRotation(Rot);
	}
	else
	{
		RecoilAccumulator = 0.f;
	}
}

void ADFCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	FirstPersonCamera->GetCameraView(DeltaTime, OutResult);
	if (IsBehindView())
	{
		// UE1 CalcBehindView(150 uu): back along the view from the eyes, short of walls
		const FVector Eye = OutResult.Location;
		const FVector Want = Eye - OutResult.Rotation.Vector() * 337.5f + FVector(0.f, 0.f, 30.f);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DFBehindView), false, this);
		if (const AActor* Riding = GetAttachParentActor())
		{
			Params.AddIgnoredActor(Riding);
		}
		OutResult.Location = GetWorld()->SweepSingleByChannel(Hit, Eye, Want, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(12.f), Params)
			? Hit.Location : Want;
	}
}

bool ADFCharacter::IsBehindView() const
{
	const ADFPlayerController* PC = Cast<ADFPlayerController>(Controller);
	return PC && PC->IsLocalController() && PC->IsBehindView();
}

// --- health / damage ---------------------------------------------------------------

int32 ADFCharacter::GetArmourCharge() const
{
	int32 Total = 0;
	for (const ADFWeapon* W : Inventory)
	{
		if (const AArmour* A = Cast<AArmour>(W))
		{
			Total += A->GetCharge();
		}
	}
	return Total;
}

bool ADFCharacter::IsHeadshot(const FVector& HitLocation) const
{
	return HitLocation.Z - GetActorLocation().Z > 0.8f * GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
}

void ADFCharacter::Heal(int32 Amount)
{
	if (HasAuthority() && IsAlive())
	{
		Health = FMath::Min(MaxHealth, Health + Amount);
	}
}

int32 ADFCharacter::AbsorbWithArmour(int32 Damage, const UDFDamageType* Type)
{
	// buffs / shields first (IDFDamageFilter components), then inventory by absorb priority
	TInlineComponentArray<UActorComponent*> Comps(this);
	for (UActorComponent* Comp : Comps)
	{
		if (IDFDamageFilter* Filter = Cast<IDFDamageFilter>(Comp); Filter && Damage > 0)
		{
			Damage = Filter->FilterDamage(Damage, Type);
		}
	}
	TArray<TObjectPtr<ADFWeapon>> Items = Inventory;   // armour may remove itself when worn out
	Items.StableSort([](const ADFWeapon& A, const ADFWeapon& B) { return A.GetAbsorbPriority() > B.GetAbsorbPriority(); });
	for (ADFWeapon* W : Items)
	{
		if (Damage <= 0)
		{
			break;
		}
		if (IsValid(W))
		{
			Damage = W->AbsorbDamage(Damage, Type);
		}
	}
	return Damage;
}

float ADFCharacter::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// CanBeDamaged: UE's `God` cheat; spawn protection: MF startup loadout + 5 s
	if (!HasAuthority() || !IsAlive() || bInLoadout || Damage <= 0.f || !CanBeDamaged() || IsSpawnProtected())
	{
		return 0.f;
	}
	if (ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>())
	{
		Damage = GM->ModifyDamage(Damage, this, EventInstigator, DamageCauser);
		if (Damage <= 0.f)
		{
			return 0.f;
		}
	}
	const UDFDamageType* Type = DamageEvent.DamageTypeClass ? Cast<UDFDamageType>(DamageEvent.DamageTypeClass->GetDefaultObject()) : nullptr;
	const int32 Applied = AbsorbWithArmour(FMath::RoundToInt(Damage), Type);
	Health -= Applied;
	// PlayTakeHitSound: 0.3 s apart; damage x rand < 8 light, < 25 medium, else heavy
	if (Applied > 0 && Health > 0 && GetWorld()->GetTimeSeconds() - LastPainSound >= 0.3f)
	{
		LastPainSound = GetWorld()->GetTimeSeconds();
		const float D = Applied * FMath::FRand();
		MulticastBodySound(uint8(EBodySound::Pain), 1.f, D < 8.f ? 0 : D < 25.f ? (FMath::FRand() < 0.5f ? 1 : 2) : 3);
	}
	Super::TakeDamage(float(Applied), DamageEvent, EventInstigator, DamageCauser);
	if (Applied > 0)
	{
		const AActor* Source = DamageCauser ? DamageCauser : (EventInstigator ? EventInstigator->GetPawn() : nullptr);
		ClientHitFlash(Source && Source != this ? (Source->GetActorLocation() - GetActorLocation()).GetSafeNormal2D() : FVector::ZeroVector);
	}
	if (Health <= 0)
	{
		ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>();
		if (GM && GM->PreventDeath(this, EventInstigator, DamageEvent.DamageTypeClass))
		{
			Health = FMath::Max(Health, 1);   // the mode decided what happens instead
		}
		else
		{
			Die(EventInstigator, DamageEvent.DamageTypeClass, DamageCauser);
		}
	}
	return float(Applied);
}

bool ADFCharacter::IsDeployed() const
{
	return CurrentWeapon && CurrentWeapon->IsDeployed();
}

bool ADFCharacter::CanJumpInternal_Implementation() const
{
	return !IsDeployed() && Super::CanJumpInternal_Implementation();   // MF PHYS_Gunnery: no jumping
}

void ADFCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	// Pawn.TakeFallingDamage: threshold -750, death -1200 uu/s; 100 * ratio^2
	const float VZ = GetCharacterMovement()->Velocity.Z;
	const float Threshold = -1687.5f * BodyScale.FallDamage;   // MF FallDamageThreshold -750
	const float Death = -2700.f * BodyScale.FallDeath;         // MF FallDeathThreshold -1200
	// Pawn.PlayLanded: impact = 0.1 x (Vz / JumpZ)^2; > 0.17 a grunt, > 0.01 the thump
	if (HasAuthority())
	{
		const float Ratio = VZ / FMath::Max(GetCharacterMovement()->JumpZVelocity, 1.f);
		const float Impact = 0.1f * Ratio * Ratio;
		if (Impact > 0.17f)
		{
			MulticastBodySound(uint8(EBodySound::LandGrunt), FMath::Min(1.f, 5.f * Impact / 4.f), 0);
		}
		if (Impact > 0.01f)
		{
			MulticastBodySound(uint8(EBodySound::Land), FMath::Clamp(4.f * Impact, 0.5f, 5.f) / 5.f, 0);
		}
	}
	if (bNoFallDamageOnce)
	{
		bNoFallDamageOnce = false;
		return;
	}
	if (HasAuthority() && VZ <= Threshold)
	{
		const float Ratio = (VZ - Threshold) / (Death - Threshold);
		FDamageEvent Ev(UDFDamage_Fell::StaticClass());
		TakeDamage(100.f * Ratio * Ratio, Ev, nullptr, nullptr);
	}
}

void ADFCharacter::SetBodyScale(const FDFBodyScale& NewScale)
{
	if (HasAuthority())
	{
		BodyScale = NewScale;
		ApplyBodyScale();
	}
}

void ADFCharacter::ApplyBodyScale()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const UCharacterMovementComponent* Def = GetDefault<ADFCharacter>(GetClass())->GetCharacterMovement();
	if (Move && Def)
	{
		Move->MaxWalkSpeed = Def->MaxWalkSpeed * BodyScale.Speed;
		Move->JumpZVelocity = Def->JumpZVelocity * BodyScale.Jump;
		Move->MaxAcceleration = Def->MaxAcceleration * BodyScale.Accel;
		Move->MaxStepHeight = Def->MaxStepHeight * BodyScale.Step;
	}
}

void ADFCharacter::FellOutOfWorld(const UDamageType& DmgType)
{
	if (HasAuthority() && IsAlive())
	{
		Die(nullptr, UDFDamage_Fell::StaticClass(), nullptr);
	}
}

void ADFCharacter::UpdateViewBob(float DeltaSeconds)
{
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	FVector Bob = FVector::ZeroVector;
	if (Move->IsMovingOnGround() && IsAlive() && !GetAttachParentActor())
	{
		// uu: WalkBob = Y * 0.4 Bob Speed sin(8 t), Z = 0.3 Bob Speed sin(16 t) (none standing)
		const float Speed = GetVelocity().Size2D() / 2.25f;
		ViewBobTime += Speed < 10.f ? 0.2f * DeltaSeconds : DeltaSeconds * (0.3f + 0.7f * Speed / FMath::Max(Move->MaxWalkSpeed / 2.25f, 1.f));
		if (Speed >= 10.f)
		{
			Bob = FVector(0.f, 0.4f * ViewBob * Speed * FMath::Sin(8.f * ViewBobTime), 0.3f * ViewBob * Speed * FMath::Sin(16.f * ViewBobTime)) * 2.25f;
		}
	}
	else
	{
		// off the ground: BobTime 0, the bob dies away (8 x a second)
		ViewBobTime = 0.f;
		Bob = ViewBobOffset * (1.f - FMath::Min(1.f, 8.f * DeltaSeconds));
	}
	if (!Bob.Equals(ViewBobOffset, 0.01f))
	{
		FirstPersonCamera->SetRelativeLocation(FirstPersonCamera->GetRelativeLocation() - ViewBobOffset + Bob);
		ViewBobOffset = Bob;
	}
}

void ADFCharacter::UpdateFootsteps(float DeltaSeconds)
{
	// RPlayer.CheckBob: BobTime runs at 0.3 + 0.7 x speed / GroundSpeed; a step each time
	// 9 x BobTime / pi passes a whole number - never while walking (the walk key)
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	const float Speed = GetVelocity().Size2D();
	const float Max = FMath::Max(Move->MaxWalkSpeed, 1.f);
	const UDFCharacterMovement* DFMove = GetMFMovement();
	const bool bWalking = IsLocallyControlled() ? (DFMove && DFMove->bWantsToWalk) : Speed < Max * 0.55f;
	if (!IsAlive() || !Move->IsMovingOnGround() || Speed < 22.5f || bWalking || GetAttachParentActor())
	{
		return;
	}
	const int32 Before = FMath::FloorToInt(0.5f * PI + 9.f * StepBobTime / PI);
	StepBobTime += DeltaSeconds * (0.3f + 0.7f * FMath::Min(Speed / Max, 1.f));
	if (FMath::FloorToInt(0.5f * PI + 9.f * StepBobTime / PI) != Before)
	{
		static const TCHAR* Steps[] = { TEXT("Sounds/RagePlayerSounds/stone01"), TEXT("Sounds/RagePlayerSounds/stone02"), TEXT("Sounds/RagePlayerSounds/stone03") };
		const FVector Foot = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight() * 0.9f);
		if (ADFZoneVolume::FindWater(GetWorld(), Foot))
		{
			DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/MiscSFX/LSplash")), Foot, 0.3f, 1000.f);   // WaterStep
		}
		else
		{
			DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(Steps[FMath::RandRange(0, 2)]), Foot, 0.6f, 1000.f);   // volume 2.2, radius 1000
		}
	}
}

void ADFCharacter::BreathTick()
{
	// HeadRegion.Zone.bWaterZone: UnderWaterTime of air, then PainTimer: 5 'Drowned' every 2 s;
	// coming up after a while: a gasp (GaspSound once it hurt, else BreathAgain)
	if (!IsAlive())
	{
		return;
	}
	const bool bUnder = ADFZoneVolume::FindWater(GetWorld(), FirstPersonCamera->GetComponentLocation()) != nullptr;
	if (bUnder)
	{
		HeadUnderFor += 1.f;
		if (HeadUnderFor > UnderWaterTime && FMath::Fmod(HeadUnderFor - UnderWaterTime, 2.f) < 1.f)
		{
			FDamageEvent Ev(UDFDamage_Drowned::StaticClass());
			TakeDamage(5.f, Ev, nullptr, nullptr);
		}
		return;
	}
	if (HeadUnderFor >= 5.f)
	{
		MulticastBodySound(uint8(EBodySound::Gasp), 0.5f, HeadUnderFor > UnderWaterTime ? 0 : 1);
	}
	HeadUnderFor = 0.f;
}

void ADFCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	if (HasAuthority())
	{
		MulticastBodySound(uint8(EBodySound::Jump), 0.7f, 0);   // PlayOwnedSound(JumpSound, , 1.5, , 1200)
	}
}

void ADFCharacter::MulticastVoice_Implementation(FName Line)
{
	UE_LOG(LogDF, Verbose, TEXT("%s says %s"), *GetName(), *Line.ToString());
	if (GetNetMode() != NM_DedicatedServer)
	{
		DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/RagePlayerVoice/") + Line.ToString()), GetActorLocation());
	}
}

void ADFCharacter::MulticastBodySound_Implementation(uint8 Kind, float Volume, uint8 Variant)
{
	if (Kind == uint8(EBodySound::Pain))
	{
		if (UDFSoldierAnimInstance* Anim = GetSoldierAnim())
		{
			Anim->OnHurt();
		}
	}
	// RPlayer.PlayHit / PlayDeathHit: hurt or dying with the head under water, bubbles come up
	if ((Kind == uint8(EBodySound::Pain) || Kind == uint8(EBodySound::Death)) && GetNetMode() != NM_DedicatedServer
		&& ADFZoneVolume::FindWater(GetWorld(), FirstPersonCamera->GetComponentLocation()))
	{
		DF::SpawnEffect(this, TEXT("Bubbles"), FirstPersonCamera->GetComponentLocation() + GetActorForwardVector() * 20.f);
	}
	// the UT male set MF ships in RagePlayerSounds (HitSound1-4, Deaths[6], JumpSound, LandGrunt)
	static const TCHAR* Pain[] = { TEXT("injurL2"), TEXT("injurL04"), TEXT("injurM04"), TEXT("injurH5") };
	static const TCHAR* Deaths[] = { TEXT("deathc1"), TEXT("deathc51"), TEXT("deathc3"), TEXT("deathc4"), TEXT("deathc53"), TEXT("deathc53") };
	FString Rel;
	float Radius = 1600.f, Pitch = 1.f;
	switch (EBodySound(Kind))
	{
	case EBodySound::Pain: Rel = FString(TEXT("Sounds/RagePlayerSounds/")) + Pain[Variant % 4]; Pitch = FMath::FRandRange(0.9f, 1.05f); break;
	case EBodySound::Death: Rel = FString(TEXT("Sounds/RagePlayerSounds/")) + Deaths[Variant % 6]; break;
	case EBodySound::Jump: Rel = TEXT("Sounds/RagePlayerSounds/jump1"); Radius = 1200.f; break;
	case EBodySound::LandGrunt: Rel = TEXT("Sounds/RagePlayerSounds/land10"); Radius = 1200.f; Pitch = FMath::FRandRange(0.8f, 1.2f); break;
	case EBodySound::Land: Rel = TEXT("Sounds/MiscSFX/Land1"); Radius = 1000.f; break;
	case EBodySound::Gasp: Rel = Variant == 0 ? TEXT("Sounds/RagePlayerSounds/hgasp1") : TEXT("Sounds/RagePlayerSounds/gasp02"); break;
	}
	DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(Rel), GetActorLocation(), Volume, Radius, Pitch);
}

void ADFCharacter::Die(AController* Killer, TSubclassOf<UDamageType> DamageType, AActor* DamageCauser)
{
	if (bDying)
	{
		return;
	}
	bDying = true;
	MulticastBodySound(uint8(EBodySound::Death), 1.f, uint8(FMath::RandRange(0, 5)));   // PlayDyingSound
	Health = FMath::Min(Health, 0);
	if (ADFVehicle* Riding = Cast<ADFVehicle>(GetAttachParentActor()))
	{
		Riding->PassengerDied(this);
	}
	AController* Victim = Controller;
	if (ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>())
	{
		GM->Killed(Killer, Victim, this, DamageType);
	}
	// GameInfo.DiscardInventory: the weapon in hand falls to the ground if it has ammo left
	if (CurrentWeapon && CurrentWeapon->GetAmmoCount() > 0)
	{
		DropWeapon(CurrentWeapon);
	}
	ClearInventory(false);
	DetachFromControllerPendingDestroy();
	MulticastOnDeath(GetVelocity().GetSafeNormal());
	SetLifeSpan(5.f);
}

void ADFCharacter::MulticastOnDeath_Implementation(FVector_NetQuantizeNormal Direction)
{
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (UDFSoldierAnimInstance* Anim = GetSoldierAnim())
	{
		Anim->OnDeath();   // RPlayer.PlayDying: one of MF's death animations, held at the end
	}
	else
	{
		// placeholder "corpse": tip the body over
		PlaceholderBody->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
		PlaceholderBody->SetRelativeLocation(FVector(0.f, 0.f, -60.f));
	}
	GetWorldTimerManager().ClearTimer(SoldierTimer);
	SetActorTickEnabled(false);
}

void ADFCharacter::OnRep_Health(int32 OldHealth)
{
}

// --- inventory ---------------------------------------------------------------------

int32 ADFCharacter::GetCurrentCarry() const
{
	int32 Total = 0;
	for (const ADFWeapon* W : Inventory)
	{
		if (W)
		{
			Total += W->CarrySize;
		}
	}
	return Total;
}

void ADFCharacter::ClientHitFlash_Implementation(FVector_NetQuantizeNormal FromDir)
{
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ADFHUD* HUD = PC->GetHUD<ADFHUD>())
		{
			HUD->NotifyPain(FromDir);
		}
	}
}

ADFWeapon* ADFCharacter::FindWeaponOfClass(TSubclassOf<ADFWeapon> WeaponClass) const
{
	for (ADFWeapon* W : Inventory)
	{
		if (W && W->GetClass() == WeaponClass)
		{
			return W;
		}
	}
	return nullptr;
}

int32 ADFCharacter::CountOfClass(TSubclassOf<ADFWeapon> WeaponClass) const
{
	int32 N = 0;
	for (const ADFWeapon* W : Inventory)
	{
		if (W && W->GetClass() == WeaponClass)
		{
			++N;
		}
	}
	return N;
}

bool ADFCharacter::CanCarry(TSubclassOf<ADFWeapon> WeaponClass) const
{
	if (!WeaponClass)
	{
		return false;
	}
	const ADFWeapon* CDO = WeaponClass->GetDefaultObject<ADFWeapon>();
	return GetCurrentCarry() + CDO->CarrySize <= MaxCarry && CountOfClass(WeaponClass) < CDO->MaxCanCarry;
}

ADFWeapon* ADFCharacter::GiveWeapon(TSubclassOf<ADFWeapon> WeaponClass, bool bEquip)
{
	if (const ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>(); GM && WeaponClass)
	{
		WeaponClass = GM->GetWeaponClassFor(this, WeaponClass);   // a mode's own variant (Zombie mode)
	}
	if (!HasAuthority() || !CanCarry(WeaponClass))
	{
		return nullptr;
	}
	FActorSpawnParameters SP;
	SP.Owner = this;
	SP.Instigator = this;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADFWeapon* W = GetWorld()->SpawnActor<ADFWeapon>(WeaponClass, GetActorTransform(), SP);
	if (!W)
	{
		return nullptr;
	}
	W->OnGiven(this);
	Inventory.Add(W);
	UpdateCarrySpeed();
	OnInventoryChanged.Broadcast(this);
	if (bEquip || !CurrentWeapon)
	{
		EquipWeapon(W);
	}
	return W;
}

void ADFCharacter::DropWeapon(ADFWeapon* Weapon)
{
	if (!HasAuthority() || !Weapon || Weapon->bUndroppable || !Inventory.Contains(Weapon))
	{
		return;
	}
	const bool bWasCurrent = CurrentWeapon == Weapon;
	RemoveWeapon(Weapon, false);
	Weapon->DropFrom(GetActorLocation(), GetActorRotation());
	if (bWasCurrent && IsAlive())
	{
		SwitchToBestWeapon();
	}
}

bool ADFCharacter::PickupWeapon(ADFWeapon* Weapon)
{
	if (!HasAuthority() || !Weapon || !Weapon->IsDropped() || !CanCarry(Weapon->GetClass()))
	{
		return false;
	}
	Weapon->OnPickedUp(this);
	Inventory.Add(Weapon);
	UpdateCarrySpeed();
	OnInventoryChanged.Broadcast(this);
	if (!CurrentWeapon)
	{
		EquipWeapon(Weapon);
	}
	return true;
}

void ADFCharacter::RemoveWeapon(ADFWeapon* Weapon, bool bDestroy)
{
	if (!Weapon)
	{
		return;
	}
	Inventory.Remove(Weapon);
	if (CurrentWeapon == Weapon)
	{
		CurrentWeapon = nullptr;
	}
	if (PendingWeapon == Weapon)
	{
		PendingWeapon = nullptr;
	}
	if (bDestroy && HasAuthority())
	{
		Weapon->Destroy();
	}
	UpdateCarrySpeed();   // MF gotcha 7H: DeleteInventory keeps the carry penalty unless recalculated
	OnInventoryChanged.Broadcast(this);
}

void ADFCharacter::ClearInventory(bool bKeepDefault)
{
	TArray<TObjectPtr<ADFWeapon>> Items = Inventory;
	for (ADFWeapon* W : Items)
	{
		if (W && !(bKeepDefault && W->bUndroppable))
		{
			RemoveWeapon(W, true);
		}
	}
	if (bKeepDefault && !CurrentWeapon)
	{
		SwitchToBestWeapon();
	}
}

void ADFCharacter::RefillAmmo()
{
	for (ADFWeapon* W : Inventory)
	{
		if (W)
		{
			W->GiveFullAmmo();
		}
	}
}

void ADFCharacter::UpdateCarrySpeed()
{
	if (UDFCharacterMovement* Move = GetMFMovement())
	{
		// Pawn.SetSpeedFromInventory: -5% per carried block, never below 50%
		Move->CarrySpeedScale = FMath::Max(0.5f, 1.f - 0.05f * float(GetCurrentCarry()));
		Move->AimSpeedScale = CurrentWeapon ? CurrentWeapon->GetMoveSpeedScale() : 1.f;
	}
}

void ADFCharacter::EquipWeapon(ADFWeapon* Weapon)
{
	if (!Weapon || Weapon == CurrentWeapon || !Weapon->bSelectable || !IsAlive())
	{
		return;
	}
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerEquipWeapon(Weapon);
	}
	if (IsLocallyControlled())
	{
		// Feedback on the key press: the put-down / bring-up (MF anim lengths) runs after
		if (const APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			if (ADFHUD* HUD = PC->GetHUD<ADFHUD>())
			{
				HUD->NotifyWeaponSelected(Weapon);
			}
		}
	}
	if (CurrentWeapon && CurrentWeapon->GetState() != EDFWeaponState::Inactive)
	{
		PendingWeapon = Weapon;
		CurrentWeapon->OnPutDown();
	}
	else
	{
		SetCurrentWeapon(Weapon, CurrentWeapon);
	}
}

void ADFCharacter::ServerEquipWeapon_Implementation(ADFWeapon* Weapon)
{
	if (Inventory.Contains(Weapon))
	{
		EquipWeapon(Weapon);
	}
}

void ADFCharacter::OnWeaponPutDown(ADFWeapon* Weapon)
{
	if (Weapon != CurrentWeapon)
	{
		return;
	}
	ADFWeapon* Next = PendingWeapon;
	PendingWeapon = nullptr;
	SetCurrentWeapon(Next, CurrentWeapon);
}

void ADFCharacter::SetCurrentWeapon(ADFWeapon* NewWeapon, ADFWeapon* OldWeapon)
{
	if (OldWeapon && OldWeapon != NewWeapon && OldWeapon->GetState() != EDFWeaponState::Inactive)
	{
		OldWeapon->ForceInactive();
	}
	CurrentWeapon = NewWeapon;
	if (NewWeapon && IsLocallyControlled())
	{
		if (const APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			if (ADFHUD* HUD = PC->GetHUD<ADFHUD>())
			{
				HUD->NotifyWeaponSelected(NewWeapon);
			}
		}
	}
	if (NewWeapon)
	{
		NewWeapon->OnEquip();
		if (bWantsFire)
		{
			NewWeapon->StartFire(false);
		}
	}
	UpdateCarrySpeed();
}

void ADFCharacter::OnRep_CurrentWeapon(ADFWeapon* OldWeapon)
{
	// The owning client predicts switches; only correct it when it disagrees.
	if (CurrentWeapon && CurrentWeapon->GetState() == EDFWeaponState::Inactive)
	{
		SetCurrentWeapon(CurrentWeapon, OldWeapon);
	}
	else if (!CurrentWeapon && OldWeapon)
	{
		OldWeapon->ForceInactive();
	}
	UpdateCarrySpeed();
}

void ADFCharacter::OnRep_Inventory()
{
	for (ADFWeapon* W : Inventory)
	{
		if (W && W->GetAttachParentActor() != this)
		{
			W->OnGiven(this);
		}
	}
	UpdateCarrySpeed();
	OnInventoryChanged.Broadcast(this);
}

void ADFCharacter::SelectInventoryGroup(int32 Group)
{
	// UE1 Weapon.WeaponChange: an empty weapon is skipped ("<item> has no ammo." when that's all
	// there is). Rage weapons kept their own ammo, so MF let you pick them and then switched away.
	TArray<ADFWeapon*> InGroup;
	const ADFWeapon* Empty = nullptr;
	for (ADFWeapon* W : Inventory)
	{
		if (W && W->bSelectable && W->GetSlotKey() == Group)
		{
			if (W->IsOutOfAmmo())
			{
				Empty = W;
				continue;
			}
			InGroup.Add(W);
		}
	}
	if (InGroup.Num() == 0)
	{
		if (Empty && IsLocallyControlled())
		{
			if (APlayerController* PC = Cast<APlayerController>(GetController()))
			{
				PC->ClientMessage(FString::Printf(TEXT("%s has no ammo."), *Empty->DisplayName.ToString()));
			}
		}
		return;
	}
	const int32 Cur = InGroup.IndexOfByKey(CurrentWeapon);
	EquipWeapon(InGroup[(Cur + 1) % InGroup.Num()]);
}

void ADFCharacter::CycleWeapon(int32 Direction)
{
	TArray<ADFWeapon*> Usable;
	for (ADFWeapon* W : Inventory)
	{
		if (W && W->bSelectable && (!W->IsOutOfAmmo() || W == CurrentWeapon))
		{
			Usable.Add(W);
		}
	}
	if (Usable.Num() < 2)
	{
		return;
	}
	Usable.StableSort([](const ADFWeapon& A, const ADFWeapon& B) { return A.InventoryGroup < B.InventoryGroup; });
	const int32 Cur = FMath::Max(0, Usable.IndexOfByKey(CurrentWeapon));
	EquipWeapon(Usable[(Cur + Direction + Usable.Num()) % Usable.Num()]);
}

void ADFCharacter::SwitchToBestWeapon()
{
	ADFWeapon* Best = nullptr;
	float BestRating = -1000.f;
	for (ADFWeapon* W : Inventory)
	{
		if (!W || !W->bSelectable || W->AutoSwitchPriority <= 0.f)
		{
			continue;
		}
		const float Rating = W->HasAnyAmmo() ? W->AIRating : -100.f;
		if (Rating > BestRating)
		{
			BestRating = Rating;
			Best = W;
		}
	}
	if (Best && Best != CurrentWeapon)
	{
		EquipWeapon(Best);
	}
	else if (!Best)
	{
		CurrentWeapon = nullptr;
	}
}

// --- input-driven actions ----------------------------------------------------------

void ADFCharacter::StartFire(bool bAlt)
{
	(bAlt ? bWantsAltFire : bWantsFire) = true;
	if (CurrentWeapon && !bInLoadout)
	{
		CurrentWeapon->StartFire(bAlt);
	}
}

void ADFCharacter::StopFire(bool bAlt)
{
	(bAlt ? bWantsAltFire : bWantsFire) = false;
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFire(bAlt);
	}
}

void ADFCharacter::Reload()
{
	if (CurrentWeapon && !bInLoadout)
	{
		CurrentWeapon->StartReload();
	}
}

void ADFCharacter::SetWalking(bool bWalk)
{
	if (UDFCharacterMovement* Move = GetMFMovement())
	{
		Move->bWantsToWalk = bWalk;
	}
}

// --- loadout -----------------------------------------------------------------------

void ADFCharacter::SetInLoadout(bool bNewInLoadout)
{
	if (!HasAuthority() || bInLoadout == bNewInLoadout)
	{
		return;
	}
	bInLoadout = bNewInLoadout;
	if (!bInLoadout && bStartupLoadout)
	{
		bStartupLoadout = false;
		SpawnProtectedUntil = GetWorld()->GetTimeSeconds() + 5.f;   // RagePlayer.EndLoadout SetAltTimer(5.0)
	}
	ApplyLoadoutVisibility();
}

bool ADFCharacter::IsSpawnProtected() const
{
	return GetWorld()->GetTimeSeconds() < SpawnProtectedUntil;
}

void ADFCharacter::OnRep_InLoadout()
{
	ApplyLoadoutVisibility();
}

void ADFCharacter::ApplyLoadoutVisibility()
{
	// MF: "as far as other players are concerned, you aren't on the level until you come out of Loadout"
	PlaceholderBody->SetHiddenInGame(bInLoadout);
	GetMesh()->SetHiddenInGame(bInLoadout, true);   // the gun in the hand too
	GetCapsuleComponent()->SetCollisionEnabled(bInLoadout ? ECollisionEnabled::QueryOnly : ECollisionEnabled::QueryAndPhysics);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel2, bInLoadout ? ECR_Ignore : ECR_Block);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, bInLoadout ? ECR_Ignore : ECR_Block);
	if (IsLocallyControlled())
	{
		// MF StartLoadout / EndLoadout sounds
		const TCHAR* Path = bInLoadout ? TEXT("/MobileForcesClassic/Sounds/MiscSFX/LoadoutEnter.LoadoutEnter")
			: TEXT("/MobileForcesClassic/Sounds/MiscSFX/LoadoutExit.LoadoutExit");
		UGameplayStatics::PlaySound2D(this, LoadObject<USoundBase>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet));
	}
	if (bInLoadout)
	{
		// MF StartLoadout: SetPhysics(PHYS_None), Velocity = 0 - the soldier stands still
		GetCharacterMovement()->StopMovementImmediately();
		if (CurrentWeapon)
		{
			CurrentWeapon->StopFire(false);
			CurrentWeapon->StopFire(true);
		}
	}
}

void ADFCharacter::SetParachute(bool bOpen)
{
	if (bOpen != bParachute)
	{
		bParachute = bOpen;
		OnRep_Parachute();
	}
}

void ADFCharacter::OnRep_Parachute()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (bParachute && !ParachuteMesh)
	{
		ParachuteMesh = NewObject<UStaticMeshComponent>(this);
		ParachuteMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ParachuteMesh->SetCastShadow(false);
		ParachuteMesh->SetupAttachment(GetCapsuleComponent());
		ParachuteMesh->SetRelativeLocation(FVector(0.f, 0.f, 90.f));   // 40 uu above
		ParachuteMesh->RegisterComponent();
		DF::ApplyPropMesh(ParachuteMesh, TEXT("parachute/Parachute"));
	}
	if (ParachuteMesh)
	{
		ParachuteMesh->SetVisibility(bParachute);
	}
	if (bParachute)
	{
		DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/parachute/parachute_sound")), GetActorLocation(), 1.f, 1600.f);
	}
}
