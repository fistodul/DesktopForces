#include "Game/BattleRoyale/BattleRoyaleActors.h"
#include "Game/BattleRoyale/BattleRoyale.h"
#include "DesktopForces.h"
#include "Core/DFProps.h"
#include "Player/DFCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "DFBattleRoyale"

namespace DFBattleRoyale {
	/** Engine shape stand-in until the converted Battle Royale props are there. */
	UStaticMesh* Shape(const TCHAR* Name) {
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
	}

	void UseMesh(UStaticMeshComponent* C, const TCHAR* Prop, const TCHAR* Fallback, const FVector& FallbackScale, const FVector& FallbackOffset = FVector::ZeroVector) {
		if (!DF::ApplyPropMesh(C, Prop)) {
			C->SetStaticMesh(Shape(Fallback));
			C->SetRelativeScale3D(FallbackScale);
			C->SetRelativeLocation(FallbackOffset);
		}
	}
} // namespace DFBattleRoyale

// --- item box --------------------------------------------------------------------------------

ABRItemBox::ABRItemBox() {
	bReplicates = true;
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->InitBoxExtent(FVector(81.f, 81.f, 60.f));
	Box->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Box->SetGenerateOverlapEvents(true);
	RootComponent = Box;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Box);
	Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Mesh->SetCanEverAffectNavigation(false);
}

void ABRItemBox::BeginPlay() {
	Super::BeginPlay();
	OnRep_Opened();
	if (HasAuthority())
		Box->OnComponentBeginOverlap.AddDynamic(this, &ABRItemBox::OnTouch);
}

void ABRItemBox::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABRItemBox, bOpened);
}

void ABRItemBox::OnRep_Opened() {
	// Gregaras' crateClosed / crateOpen models (CC0), standing on the floor (bottom 18 cm under the origin)
	DFBattleRoyale::UseMesh(Mesh, bOpened ? TEXT("battleRoyale/crateOpen") : TEXT("battleRoyale/crateClosed"),
		TEXT("Cube"), FVector(0.72f, 0.36f, bOpened ? 0.2f : 0.32f), FVector(0, 0, -44.f));
	if (Mesh->GetStaticMesh() != DFBattleRoyale::Shape(TEXT("Cube")))
		Mesh->SetRelativeLocation(FVector(0, 0, -60.f + 18.f));
}

void ABRItemBox::OnTouch(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep) {
	const ADFCharacter* C = Cast<ADFCharacter>(Other);
	const ABattleRoyale* GM = GetWorld()->GetAuthGameMode<ABattleRoyale>();
	if (bOpened || !C || !C->IsAlive() || !GM)
		return;
	// itemBox.touch: five random weapons out of the mode's list
	for (int32 i = 0; i < ItemCount; ++i) {
		UClass* Class = GM->PickItem();
		if (!Class)
			continue;
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ADFWeapon* W = GetWorld()->SpawnActor<ADFWeapon>(Class, GetActorTransform(), SP)) {
			W->GiveFullAmmo();
			W->DropFrom(GetActorLocation() + FVector(0, 0, 40.f), FRotator(0.f, i * 72.f, 0.f));
		}
	}
	bOpened = true;
	OnRep_Opened();
	ForceNetUpdate();
}

// --- tower / platform ------------------------------------------------------------------------

ABRTower::ABRTower() {
	bReplicates = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCanEverAffectNavigation(false);
	RootComponent = Mesh;
}

void ABRTower::BeginPlay() {
	Super::BeginPlay();
	ApplyLook();
}

void ABRTower::ApplyLook() {
	// 215 x 215 x 474 cm (metalTower, origin at the foot)
	if (!DF::ApplyPropMesh(Mesh, TEXT("battleRoyale/metalTower"))) {
		Mesh->SetStaticMesh(DFBattleRoyale::Shape(TEXT("Cylinder")));
		Mesh->SetRelativeScale3D(FVector(2.15f, 2.15f, 4.74f));
	}
}

float ABRTower::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) {
	if (!HasAuthority() || Damage <= 0.f || IsActorBeingDestroyed())
		return 0.f;
	const int32 D = FMath::Max(1, FMath::RoundToInt(Damage));
	Health -= D;
	// metalTower.TakeDamage: builder ammo for every 50 damage, around the foot
	if (DamageForAmmo > 0) {
		for (AmmoDamage += D; AmmoDamage > DamageForAmmo; AmmoDamage -= DamageForAmmo) {
			const FVector At = GetActorLocation() + FVector(FMath::FRandRange(0.f, 337.f), FMath::FRandRange(0.f, 337.f), 225.f);
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			GetWorld()->SpawnActor<ABRBuilderAmmo>(ABRBuilderAmmo::StaticClass(), At, FRotator::ZeroRotator, SP);
		}
	}
	if (Health <= 0)
		Destroy();
	return float(D);
}

ABRPlatform::ABRPlatform() {
	Health = 150;
	DamageForAmmo = 0;
	SetReplicatingMovement(true);
}

void ABRPlatform::ApplyLook() {
	// 664 x 704 x 48 cm, centred
	if (!DF::ApplyPropMesh(Mesh, TEXT("battleRoyale/platform"))) {
		Mesh->SetStaticMesh(DFBattleRoyale::Shape(TEXT("Cube")));
		Mesh->SetRelativeScale3D(FVector(6.64f, 7.04f, 0.48f));
	}
}

// --- builder ammo ----------------------------------------------------------------------------

ABRBuilderAmmo::ABRBuilderAmmo() {
	bReplicates = true;
	SetReplicatingMovement(true);
	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->InitSphereRadius(40.f);
	Sphere->SetCollisionProfileName(TEXT("PhysicsActor"));
	Sphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Sphere->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
	Sphere->SetGenerateOverlapEvents(true);
	Sphere->SetSimulatePhysics(true); // PHYS_Falling
	Sphere->SetCanEverAffectNavigation(false);
	RootComponent = Sphere;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Sphere);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InitialLifeSpan = 60.f;
}

void ABRBuilderAmmo::BeginPlay() {
	Super::BeginPlay();
	DFBattleRoyale::UseMesh(Mesh, TEXT("Rage/unitball"), TEXT("Sphere"), FVector(0.4f));
	if (HasAuthority())
		Sphere->OnComponentBeginOverlap.AddDynamic(this, &ABRBuilderAmmo::OnTouch);
}

void ABRBuilderAmmo::OnTouch(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep) {
	ADFCharacter* C = Cast<ADFCharacter>(Other);
	if (!C || !C->IsAlive() || IsActorBeingDestroyed())
		return;
	// builderAmmo.touch: the builder with 5 platforms, or 5 more
	ABRBuilder* B = Cast<ABRBuilder>(C->FindWeaponOfClass(ABRBuilder::StaticClass()));
	if (!B)
		B = Cast<ABRBuilder>(C->GiveWeapon(ABRBuilder::StaticClass(), false));
	if (B) {
		B->AddPlatforms(5);
		Destroy();
	}
}

// --- builder ---------------------------------------------------------------------------------

ABRBuilder::ABRBuilder() {
	DisplayName = LOCTEXT("Builder", "Builder");
	InventoryGroup = 1; // builder: InventoryGroup 1, CarrySize 0
	CarrySize = 0;
	MaxClipAmmo = 999;
	MaxClips = 1;
	bDestroyWhenEmpty = false;
	bLoadoutSelectable = false;
	FireTime = 0.3f;
	EquipTime = 0.3f;
	PutDownTime = 0.2f;
}

void ABRBuilder::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABRBuilder, Angle);
}

void ABRBuilder::AddPlatforms(int32 Count) {
	ClipAmmo = FMath::Min(ClipAmmo + Count, MaxClipAmmo);
	ForceNetUpdate();
}

FTransform ABRBuilder::GetPlacement() const {
	const ADFCharacter* C = GetOwnerCharacter();
	if (!C)
		return GetActorTransform();
	// spawnPlatform: Location + vector(ViewRotation) * 300, pitch from the builder, yaw from the body
	const FRotator View = GetAimRotation();
	return FTransform(FRotator(Pitches[FMath::Clamp(Angle, 0, 2)], C->GetActorRotation().Yaw, 0.f),
		C->GetActorLocation() + View.Vector() * 675.f);
}

bool ABRBuilder::TryFire(bool bAlt) {
	ADFCharacter* C = GetOwnerCharacter();
	if (!C || !C->IsAlive())
		return false;
	SetState(EDFWeaponState::Firing, FireTime);
	if (bAlt) {
		Angle = (Angle + 1) % 3;
		return true;
	}
	if (ClipAmmo < 1)
		return false;
	UseAmmo();
	if (HasAuthority()) {
		FActorSpawnParameters SP;
		SP.Instigator = C;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<ABRPlatform>(ABRPlatform::StaticClass(), GetPlacement(), SP);
	}
	return true;
}

void ABRBuilder::OnEquip() {
	Super::OnEquip();
	if (!IsLocallyControlled())
		return;
	// a ghost of the next platform where it would go (builder.platformPreview)
	if (!Preview) {
		Preview = NewObject<UStaticMeshComponent>(this, TEXT("Preview"));
		Preview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Preview->SetCastShadow(false);
		Preview->SetUsingAbsoluteLocation(true);
		Preview->SetUsingAbsoluteRotation(true);
		Preview->SetUsingAbsoluteScale(true);
		Preview->SetupAttachment(RootComponent);
		Preview->RegisterComponent();
		if (!DF::ApplyPropMesh(Preview, TEXT("battleRoyale/platform"))) {
			Preview->SetStaticMesh(DFBattleRoyale::Shape(TEXT("Cube")));
			Preview->SetRelativeScale3D(FVector(6.64f, 7.04f, 0.48f));
		}
		Preview->SetRenderInDepthPass(false);
	}
	Preview->SetVisibility(true);
	SetActorTickEnabled(true);
}

void ABRBuilder::OnPutDown() {
	Super::OnPutDown();
	if (Preview)
		Preview->SetVisibility(false);
}

void ABRBuilder::Destroyed() {
	if (Preview)
		Preview->DestroyComponent();
	Super::Destroyed();
}

void ABRBuilder::Tick(float DeltaSeconds) {
	if (IsDropped()) {
		Super::Tick(DeltaSeconds); // falling to the floor
		return;
	}
	AActor::Tick(DeltaSeconds);
	const ADFCharacter* C = GetOwnerCharacter();
	const bool bShow = Preview && C && C->GetCurrentWeapon() == this && C->IsAlive();
	if (Preview)
		Preview->SetVisibility(bShow);
	if (!bShow) {
		SetActorTickEnabled(false);
		return;
	}
	const FTransform T = GetPlacement();
	Preview->SetWorldLocationAndRotation(T.GetLocation(), T.GetRotation());
}

// --- safe zone -------------------------------------------------------------------------------

ABRSafeZone::ABRSafeZone() {
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetCanEverAffectNavigation(false);
	RootComponent = Mesh;
}

void ABRSafeZone::BeginPlay() {
	Super::BeginPlay();
	// safeZoneTrans: Gregaras' translucent sphere (5.76 uu across at draw scale 1). Only shown
	// when its material really is see-through (it is not without the classic texture).
	if (DF::ApplyPropMesh(Mesh, TEXT("battleRoyale/safeZoneTrans"))) {
		MeshRadius = FMath::Max(1.f, Mesh->GetStaticMesh()->GetBoundingBox().GetExtent().X);
		const UMaterialInterface* M = Mesh->GetMaterial(0);
		const EBlendMode Blend = M ? M->GetBlendMode() : BLEND_Opaque;
		Mesh->SetVisibility(Blend == BLEND_Translucent || Blend == BLEND_Additive || Blend == BLEND_Modulate);
	} else {
		Mesh->SetVisibility(false);
	}
	if (HasAuthority())
		GetWorldTimerManager().SetTimer(HurtTimer, this, &ABRSafeZone::HurtOutside, 1.f, true); // safeZone.timer: every second
}

void ABRSafeZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABRSafeZone, Radius);
}

void ABRSafeZone::Tick(float DeltaSeconds) {
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && bShrinking && Radius > MinRadius)
		Radius = FMath::Max(MinRadius, Radius - ShrinkRate * DeltaSeconds);
	Mesh->SetWorldScale3D(FVector(Radius / MeshRadius));
}

void ABRSafeZone::HurtOutside() {
	TArray<ADFCharacter*> Outside;
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It)
		if (It->IsAlive() && It->GetController() && !IsInside(It->GetActorLocation()))
			Outside.Add(*It);
	for (ADFCharacter* C : Outside) {
		FDamageEvent Ev(UDFDamage_SafeZone::StaticClass());
		C->TakeDamage(float(OutsideDamage), Ev, nullptr, this);
	}
}

#undef LOCTEXT_NAMESPACE
