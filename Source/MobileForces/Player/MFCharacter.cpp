#include "Player/MFCharacter.h"
#include "Player/MFCharacterMovement.h"
#include "Weapons/MFWeapon.h"
#include "Weapons/MFWeapons.h"
#include "Game/MFGameMode.h"
#include "Game/MFPlayerState.h"
#include "UI/MFHUD.h"
#include "MobileForces.h"
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
	DOREPLIFETIME(AMFCharacter, bDying);
	DOREPLIFETIME(AMFCharacter, Inventory);
	DOREPLIFETIME(AMFCharacter, CurrentWeapon);
	DOREPLIFETIME(AMFCharacter, bInLoadout);
}

void AMFCharacter::BeginPlay()
{
	Super::BeginPlay();
	Health = MaxHealth;
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
}

void AMFCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	SetActorTickEnabled(IsLocallyControlled());   // owning client: camera FOV + recoil
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
	return PS ? PS->GetTeam() : MF::NoTeam;
}

float AMFCharacter::GetDesiredFOV() const
{
	return DefaultFOV * (CurrentWeapon ? CurrentWeapon->GetFOVScale() : 1.f);
}

void AMFCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

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
	TArray<TObjectPtr<AMFWeapon>> Items = Inventory;   // armour may remove itself when worn out
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
		Damage = GM->ModifyDamage(Damage, this, EventInstigator);
		if (Damage <= 0.f)
		{
			return 0.f;
		}
	}
	const UMFDamageType* Type = DamageEvent.DamageTypeClass ? Cast<UMFDamageType>(DamageEvent.DamageTypeClass->GetDefaultObject()) : nullptr;
	const int32 Applied = AbsorbWithArmour(FMath::RoundToInt(Damage), Type);
	Health -= Applied;
	Super::TakeDamage(float(Applied), DamageEvent, EventInstigator, DamageCauser);
	if (Applied > 0)
	{
		const AActor* Source = DamageCauser ? DamageCauser : (EventInstigator ? EventInstigator->GetPawn() : nullptr);
		ClientHitFlash(Source && Source != this ? (Source->GetActorLocation() - GetActorLocation()).GetSafeNormal2D() : FVector::ZeroVector);
	}
	if (Health <= 0)
	{
		Die(EventInstigator, DamageEvent.DamageTypeClass, DamageCauser);
	}
	return float(Applied);
}

void AMFCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	// Pawn.TakeFallingDamage: threshold -750, death -1200 uu/s; 100 * ratio^2
	const float VZ = GetCharacterMovement()->Velocity.Z;
	constexpr float Threshold = -1687.5f;   // MF FallDamageThreshold -750
	constexpr float Death = -2700.f;        // MF FallDeathThreshold -1200
	if (HasAuthority() && VZ <= Threshold)
	{
		const float Ratio = (VZ - Threshold) / (Death - Threshold);
		FDamageEvent Ev(UMFDamage_Fell::StaticClass());
		TakeDamage(100.f * Ratio * Ratio, Ev, nullptr, nullptr);
	}
}

void AMFCharacter::FellOutOfWorld(const UDamageType& DmgType)
{
	if (HasAuthority() && IsAlive())
	{
		Die(nullptr, UMFDamage_Fell::StaticClass(), nullptr);
	}
}

void AMFCharacter::Die(AController* Killer, TSubclassOf<UDamageType> DamageType, AActor* DamageCauser)
{
	if (bDying)
	{
		return;
	}
	bDying = true;
	Health = FMath::Min(Health, 0);
	AController* Victim = Controller;
	if (AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>())
	{
		GM->Killed(Killer, Victim, this, DamageType);
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
	TArray<AMFWeapon*> InGroup;
	for (AMFWeapon* W : Inventory)
	{
		if (W && W->bSelectable && W->GetSlotKey() == Group)
		{
			InGroup.Add(W);
		}
	}
	if (InGroup.Num() == 0)
	{
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
		if (W && W->bSelectable)
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
		const TCHAR* Path = bInLoadout ? TEXT("/Game/MFImported/Sounds/MiscSFX/LoadoutEnter.LoadoutEnter")
			: TEXT("/Game/MFImported/Sounds/MiscSFX/LoadoutExit.LoadoutExit");
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
