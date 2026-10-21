#include "Player/MFCharacter.h"
#include "Sound/SoundBase.h"
#include "Core/MFContent.h"
#include "World/MFZoneVolume.h"
#include "Vehicles/MFVehicle.h"
#include "Core/MFUserSettings.h"
#include "Player/MFCharacterMovement.h"
#include "Weapons/MFWeapon.h"
#include "Weapons/MFWeapons.h"
#include "Game/MFGameMode.h"
#include "Game/MFPlayerState.h"
#include "UI/MFHUD.h"
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

AMFCharacter::AMFCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UMFCharacterMovement>(ACharacter::CharacterMovementComponentName))
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

	DefaultInventory.Add(AMFWeapon_Knife::StaticClass());

	SetNetUpdateFrequency(30.f);
	SetMinNetUpdateFrequency(10.f);
}

UMFCharacterMovement* AMFCharacter::GetMFMovement() const
{
	return Cast<UMFCharacterMovement>(GetCharacterMovement());
}

void AMFCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFCharacter, Health);
	DOREPLIFETIME(AMFCharacter, MaxHealth);
	DOREPLIFETIME(AMFCharacter, MaxCarry);
	DOREPLIFETIME(AMFCharacter, BodyScale);
	DOREPLIFETIME(AMFCharacter, bDying);
	DOREPLIFETIME(AMFCharacter, Inventory);
	DOREPLIFETIME(AMFCharacter, CurrentWeapon);
	DOREPLIFETIME(AMFCharacter, bInLoadout);
}

void AMFCharacter::BeginPlay()
{
	Super::BeginPlay();
	Health = MaxHealth;
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(BreathTimer, this, &AMFCharacter::BreathTick, 1.f, true);
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
}

void AMFCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	SetActorTickEnabled(IsLocallyControlled());
	ResumeWeapon();
}

void AMFCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	SetActorTickEnabled(IsLocallyControlled());   // owning client: camera FOV + recoil
	ApplyUserSettings();
	ResumeWeapon();
}

void AMFCharacter::ResumeWeapon()
{
	// back from a vehicle's driving seat (AMFVehicle::TryEnter put the weapon away): bring it up
	if (CurrentWeapon && IsAlive() && CurrentWeapon->GetState() == EMFWeaponState::Inactive && !PendingWeapon)
	{
		CurrentWeapon->OnEquip();
	}
}

void AMFCharacter::ApplyUserSettings()
{
	if (const UMFUserSettings* S = UMFUserSettings::Get(); S && IsLocallyControlled())
	{
		DefaultFOV = FMath::Clamp(S->FieldOfView, 60.f, 140.f);
		FirstPersonCamera->SetFieldOfView(GetDesiredFOV());
	}
}

void AMFCharacter::Destroyed()
{
	if (HasAuthority())
	{
		ClearInventory(false);
	}
	Super::Destroyed();
}

// --- team / camera -----------------------------------------------------------------

uint8 AMFCharacter::GetTeam() const
{
	const AMFPlayerState* PS = GetPlayerState<AMFPlayerState>();
	if (!PS)
	{
		// riding in a vehicle: the controller (and its player state) is on the vehicle
		if (const APawn* Ride = Cast<APawn>(GetAttachParentActor()))
		{
			PS = Ride->GetPlayerState<AMFPlayerState>();
		}
	}
	return PS ? PS->GetTeam() : MF::NoTeam;
}

float AMFCharacter::GetDesiredFOV() const
{
	return DefaultFOV * (CurrentWeapon ? CurrentWeapon->GetFOVScale() : 1.f);
}

void AMFCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
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

void AMFCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	FirstPersonCamera->GetCameraView(DeltaTime, OutResult);
}

// --- health / damage ---------------------------------------------------------------

int32 AMFCharacter::GetArmourCharge() const
{
	int32 Total = 0;
	for (const AMFWeapon* W : Inventory)
	{
		if (const AMFWeapon_Armour* A = Cast<AMFWeapon_Armour>(W))
		{
			Total += A->GetCharge();
		}
	}
	return Total;
}

bool AMFCharacter::IsHeadshot(const FVector& HitLocation) const
{
	return HitLocation.Z - GetActorLocation().Z > 0.8f * GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
}

void AMFCharacter::Heal(int32 Amount)
{
	if (HasAuthority() && IsAlive())
	{
		Health = FMath::Min(MaxHealth, Health + Amount);
	}
}

int32 AMFCharacter::AbsorbWithArmour(int32 Damage, const UMFDamageType* Type)
{
	// buffs / shields first (IMFDamageFilter components), then inventory by absorb priority
	TInlineComponentArray<UActorComponent*> Comps(this);
	for (UActorComponent* Comp : Comps)
	{
		if (IMFDamageFilter* Filter = Cast<IMFDamageFilter>(Comp); Filter && Damage > 0)
		{
			Damage = Filter->FilterDamage(Damage, Type);
		}
	}
	TArray<TObjectPtr<AMFWeapon>> Items = Inventory;   // armour may remove itself when worn out
	Items.StableSort([](const AMFWeapon& A, const AMFWeapon& B) { return A.GetAbsorbPriority() > B.GetAbsorbPriority(); });
	for (AMFWeapon* W : Items)
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

float AMFCharacter::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// CanBeDamaged: UE's `God` cheat; spawn protection: MF startup loadout + 5 s
	if (!HasAuthority() || !IsAlive() || bInLoadout || Damage <= 0.f || !CanBeDamaged() || IsSpawnProtected())
	{
		return 0.f;
	}
	if (AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>())
	{
		Damage = GM->ModifyDamage(Damage, this, EventInstigator, DamageCauser);
		if (Damage <= 0.f)
		{
			return 0.f;
		}
	}
	const UMFDamageType* Type = DamageEvent.DamageTypeClass ? Cast<UMFDamageType>(DamageEvent.DamageTypeClass->GetDefaultObject()) : nullptr;
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
		AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>();
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

bool AMFCharacter::IsDeployed() const
{
	return CurrentWeapon && CurrentWeapon->IsDeployed();
}

bool AMFCharacter::CanJumpInternal_Implementation() const
{
	return !IsDeployed() && Super::CanJumpInternal_Implementation();   // MF PHYS_Gunnery: no jumping
}

void AMFCharacter::Landed(const FHitResult& Hit)
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
		FDamageEvent Ev(UMFDamage_Fell::StaticClass());
		TakeDamage(100.f * Ratio * Ratio, Ev, nullptr, nullptr);
	}
}

void AMFCharacter::SetBodyScale(const FMFBodyScale& NewScale)
{
	if (HasAuthority())
	{
		BodyScale = NewScale;
		ApplyBodyScale();
	}
}

void AMFCharacter::ApplyBodyScale()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const UCharacterMovementComponent* Def = GetDefault<AMFCharacter>(GetClass())->GetCharacterMovement();
	if (Move && Def)
	{
		Move->MaxWalkSpeed = Def->MaxWalkSpeed * BodyScale.Speed;
		Move->JumpZVelocity = Def->JumpZVelocity * BodyScale.Jump;
		Move->MaxAcceleration = Def->MaxAcceleration * BodyScale.Accel;
		Move->MaxStepHeight = Def->MaxStepHeight * BodyScale.Step;
	}
}

void AMFCharacter::FellOutOfWorld(const UDamageType& DmgType)
{
	if (HasAuthority() && IsAlive())
	{
		Die(nullptr, UMFDamage_Fell::StaticClass(), nullptr);
	}
}

void AMFCharacter::UpdateFootsteps(float DeltaSeconds)
{
	// RPlayer.CheckBob: BobTime runs at 0.3 + 0.7 x speed / GroundSpeed; a step each time
	// 9 x BobTime / pi passes a whole number - never while walking (the walk key)
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	const float Speed = GetVelocity().Size2D();
	const float Max = FMath::Max(Move->MaxWalkSpeed, 1.f);
	const UMFCharacterMovement* MFMove = GetMFMovement();
	const bool bWalking = IsLocallyControlled() ? (MFMove && MFMove->bWantsToWalk) : Speed < Max * 0.55f;
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
		if (AMFZoneVolume::FindWater(GetWorld(), Foot))
		{
			MF::PlaySoundRadius(this, MF::LoadClassic<USoundBase>(TEXT("Sounds/MiscSFX/LSplash")), Foot, 0.3f, 1000.f);   // WaterStep
		}
		else
		{
			MF::PlaySoundRadius(this, MF::LoadClassic<USoundBase>(Steps[FMath::RandRange(0, 2)]), Foot, 0.6f, 1000.f);   // volume 2.2, radius 1000
		}
	}
}

void AMFCharacter::BreathTick()
{
	// HeadRegion.Zone.bWaterZone: UnderWaterTime of air, then PainTimer: 5 'Drowned' every 2 s;
	// coming up after a while: a gasp (GaspSound once it hurt, else BreathAgain)
	if (!IsAlive())
	{
		return;
	}
	const bool bUnder = AMFZoneVolume::FindWater(GetWorld(), FirstPersonCamera->GetComponentLocation()) != nullptr;
	if (bUnder)
	{
		HeadUnderFor += 1.f;
		if (HeadUnderFor > UnderWaterTime && FMath::Fmod(HeadUnderFor - UnderWaterTime, 2.f) < 1.f)
		{
			FDamageEvent Ev(UMFDamage_Drowned::StaticClass());
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

void AMFCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	if (HasAuthority())
	{
		MulticastBodySound(uint8(EBodySound::Jump), 0.7f, 0);   // PlayOwnedSound(JumpSound, , 1.5, , 1200)
	}
}

void AMFCharacter::MulticastVoice_Implementation(FName Line)
{
	UE_LOG(LogMF, Verbose, TEXT("%s says %s"), *GetName(), *Line.ToString());
	if (GetNetMode() != NM_DedicatedServer)
	{
		MF::PlaySoundRadius(this, MF::LoadClassic<USoundBase>(TEXT("Sounds/RagePlayerVoice/") + Line.ToString()), GetActorLocation());
	}
}

void AMFCharacter::MulticastBodySound_Implementation(uint8 Kind, float Volume, uint8 Variant)
{
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
	MF::PlaySoundRadius(this, MF::LoadClassic<USoundBase>(Rel), GetActorLocation(), Volume, Radius, Pitch);
}

void AMFCharacter::Die(AController* Killer, TSubclassOf<UDamageType> DamageType, AActor* DamageCauser)
{
	if (bDying)
	{
		return;
	}
	bDying = true;
	MulticastBodySound(uint8(EBodySound::Death), 1.f, uint8(FMath::RandRange(0, 5)));   // PlayDyingSound
	Health = FMath::Min(Health, 0);
	if (AMFVehicle* Riding = Cast<AMFVehicle>(GetAttachParentActor()))
	{
		Riding->PassengerDied(this);
	}
	AController* Victim = Controller;
	if (AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>())
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

void AMFCharacter::MulticastOnDeath_Implementation(FVector_NetQuantizeNormal Direction)
{
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Placeholder "corpse": tip the body over. Replace with ragdoll once MF meshes exist.
	PlaceholderBody->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
	PlaceholderBody->SetRelativeLocation(FVector(0.f, 0.f, -60.f));
	SetActorTickEnabled(false);
}

void AMFCharacter::OnRep_Health(int32 OldHealth)
{
}

// --- inventory ---------------------------------------------------------------------

int32 AMFCharacter::GetCurrentCarry() const
{
	int32 Total = 0;
	for (const AMFWeapon* W : Inventory)
	{
		if (W)
		{
			Total += W->CarrySize;
		}
	}
	return Total;
}

void AMFCharacter::ClientHitFlash_Implementation(FVector_NetQuantizeNormal FromDir)
{
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (AMFHUD* HUD = PC->GetHUD<AMFHUD>())
		{
			HUD->NotifyPain(FromDir);
		}
	}
}

AMFWeapon* AMFCharacter::FindWeaponOfClass(TSubclassOf<AMFWeapon> WeaponClass) const
{
	for (AMFWeapon* W : Inventory)
	{
		if (W && W->GetClass() == WeaponClass)
		{
			return W;
		}
	}
	return nullptr;
}

int32 AMFCharacter::CountOfClass(TSubclassOf<AMFWeapon> WeaponClass) const
{
	int32 N = 0;
	for (const AMFWeapon* W : Inventory)
	{
		if (W && W->GetClass() == WeaponClass)
		{
			++N;
		}
	}
	return N;
}

bool AMFCharacter::CanCarry(TSubclassOf<AMFWeapon> WeaponClass) const
{
	if (!WeaponClass)
	{
		return false;
	}
	const AMFWeapon* CDO = WeaponClass->GetDefaultObject<AMFWeapon>();
	return GetCurrentCarry() + CDO->CarrySize <= MaxCarry && CountOfClass(WeaponClass) < CDO->MaxCanCarry;
}

AMFWeapon* AMFCharacter::GiveWeapon(TSubclassOf<AMFWeapon> WeaponClass, bool bEquip)
{
	if (const AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>(); GM && WeaponClass)
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
	AMFWeapon* W = GetWorld()->SpawnActor<AMFWeapon>(WeaponClass, GetActorTransform(), SP);
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

void AMFCharacter::DropWeapon(AMFWeapon* Weapon)
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

bool AMFCharacter::PickupWeapon(AMFWeapon* Weapon)
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

void AMFCharacter::RemoveWeapon(AMFWeapon* Weapon, bool bDestroy)
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

void AMFCharacter::ClearInventory(bool bKeepDefault)
{
	TArray<TObjectPtr<AMFWeapon>> Items = Inventory;
	for (AMFWeapon* W : Items)
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

void AMFCharacter::RefillAmmo()
{
	for (AMFWeapon* W : Inventory)
	{
		if (W)
		{
			W->GiveFullAmmo();
		}
	}
}

void AMFCharacter::UpdateCarrySpeed()
{
	if (UMFCharacterMovement* Move = GetMFMovement())
	{
		// Pawn.SetSpeedFromInventory: -5% per carried block, never below 50%
		Move->CarrySpeedScale = FMath::Max(0.5f, 1.f - 0.05f * float(GetCurrentCarry()));
		Move->AimSpeedScale = CurrentWeapon ? CurrentWeapon->GetMoveSpeedScale() : 1.f;
	}
}

void AMFCharacter::EquipWeapon(AMFWeapon* Weapon)
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
			if (AMFHUD* HUD = PC->GetHUD<AMFHUD>())
			{
				HUD->NotifyWeaponSelected(Weapon);
			}
		}
	}
	if (CurrentWeapon && CurrentWeapon->GetState() != EMFWeaponState::Inactive)
	{
		PendingWeapon = Weapon;
		CurrentWeapon->OnPutDown();
	}
	else
	{
		SetCurrentWeapon(Weapon, CurrentWeapon);
	}
}

void AMFCharacter::ServerEquipWeapon_Implementation(AMFWeapon* Weapon)
{
	if (Inventory.Contains(Weapon))
	{
		EquipWeapon(Weapon);
	}
}

void AMFCharacter::OnWeaponPutDown(AMFWeapon* Weapon)
{
	if (Weapon != CurrentWeapon)
	{
		return;
	}
	AMFWeapon* Next = PendingWeapon;
	PendingWeapon = nullptr;
	SetCurrentWeapon(Next, CurrentWeapon);
}

void AMFCharacter::SetCurrentWeapon(AMFWeapon* NewWeapon, AMFWeapon* OldWeapon)
{
	if (OldWeapon && OldWeapon != NewWeapon && OldWeapon->GetState() != EMFWeaponState::Inactive)
	{
		OldWeapon->ForceInactive();
	}
	CurrentWeapon = NewWeapon;
	if (NewWeapon && IsLocallyControlled())
	{
		if (const APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			if (AMFHUD* HUD = PC->GetHUD<AMFHUD>())
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

void AMFCharacter::OnRep_CurrentWeapon(AMFWeapon* OldWeapon)
{
	// The owning client predicts switches; only correct it when it disagrees.
	if (CurrentWeapon && CurrentWeapon->GetState() == EMFWeaponState::Inactive)
	{
		SetCurrentWeapon(CurrentWeapon, OldWeapon);
	}
	else if (!CurrentWeapon && OldWeapon)
	{
		OldWeapon->ForceInactive();
	}
	UpdateCarrySpeed();
}

void AMFCharacter::OnRep_Inventory()
{
	for (AMFWeapon* W : Inventory)
	{
		if (W && W->GetAttachParentActor() != this)
		{
			W->OnGiven(this);
		}
	}
	UpdateCarrySpeed();
	OnInventoryChanged.Broadcast(this);
}

void AMFCharacter::SelectInventoryGroup(int32 Group)
{
	// UE1 Weapon.WeaponChange: an empty weapon is skipped ("<item> has no ammo." when that's all
	// there is). Rage weapons kept their own ammo, so MF let you pick them and then switched away.
	TArray<AMFWeapon*> InGroup;
	const AMFWeapon* Empty = nullptr;
	for (AMFWeapon* W : Inventory)
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

void AMFCharacter::CycleWeapon(int32 Direction)
{
	TArray<AMFWeapon*> Usable;
	for (AMFWeapon* W : Inventory)
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
	Usable.StableSort([](const AMFWeapon& A, const AMFWeapon& B) { return A.InventoryGroup < B.InventoryGroup; });
	const int32 Cur = FMath::Max(0, Usable.IndexOfByKey(CurrentWeapon));
	EquipWeapon(Usable[(Cur + Direction + Usable.Num()) % Usable.Num()]);
}

void AMFCharacter::SwitchToBestWeapon()
{
	AMFWeapon* Best = nullptr;
	float BestRating = -1000.f;
	for (AMFWeapon* W : Inventory)
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

void AMFCharacter::StartFire(bool bAlt)
{
	(bAlt ? bWantsAltFire : bWantsFire) = true;
	if (CurrentWeapon && !bInLoadout)
	{
		CurrentWeapon->StartFire(bAlt);
	}
}

void AMFCharacter::StopFire(bool bAlt)
{
	(bAlt ? bWantsAltFire : bWantsFire) = false;
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFire(bAlt);
	}
}

void AMFCharacter::Reload()
{
	if (CurrentWeapon && !bInLoadout)
	{
		CurrentWeapon->StartReload();
	}
}

void AMFCharacter::SetWalking(bool bWalk)
{
	if (UMFCharacterMovement* Move = GetMFMovement())
	{
		Move->bWantsToWalk = bWalk;
	}
}

// --- loadout -----------------------------------------------------------------------

void AMFCharacter::SetInLoadout(bool bNewInLoadout)
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

bool AMFCharacter::IsSpawnProtected() const
{
	return GetWorld()->GetTimeSeconds() < SpawnProtectedUntil;
}

void AMFCharacter::OnRep_InLoadout()
{
	ApplyLoadoutVisibility();
}

void AMFCharacter::ApplyLoadoutVisibility()
{
	// MF: "as far as other players are concerned, you aren't on the level until you come out of Loadout"
	PlaceholderBody->SetHiddenInGame(bInLoadout);
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
