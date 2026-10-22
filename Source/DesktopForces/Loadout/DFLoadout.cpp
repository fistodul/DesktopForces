#include "Loadout/DFLoadout.h"
#include "Game/DFGameState.h"
#include "Player/DFCharacter.h"
#include "Weapons/DFWeapon.h"
#include "Core/DFGameSettings.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

const TArray<TSubclassOf<ADFWeapon>>& UDFLoadoutLibrary::DefaultItems()
{
	return UDFGameSettings::Get().GetLoadoutItems();
}

ADFLoadoutSource* UDFLoadoutLibrary::FindLoadoutSourceFor(const ADFCharacter* Character)
{
	if (!Character)
	{
		return nullptr;
	}
	for (TActorIterator<ADFLoadoutSource> It(Character->GetWorld()); It; ++It)
	{
		if (It->IsUsableBy(Character) && It->IsInside(Character))
		{
			return *It;
		}
	}
	return nullptr;
}

// --- source ----------------------------------------------------------------------------

ADFLoadoutSource::ADFLoadoutSource()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;   // static map actors exist on every machine already
}

const TArray<TSubclassOf<ADFWeapon>>& ADFLoadoutSource::GetItems() const
{
	return Items.Num() > 0 ? Items : UDFLoadoutLibrary::DefaultItems();
}

void ADFLoadoutSource::BeginPlay()
{
	Super::BeginPlay();
}

bool ADFLoadoutSource::IsRelevantNow() const
{
	const ADFGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADFGameState>() : nullptr;
	const bool bTeamGame = GS && GS->IsTeamGame();
	return bTeamGame ? bTeamPlay : bNonTeamPlay;
}

bool ADFLoadoutSource::IsUsableBy(const ADFCharacter* Character) const
{
	if (!Character || !Character->IsAlive() || !IsRelevantNow())
	{
		return false;
	}
	const ADFGameState* GS = GetWorld()->GetGameState<ADFGameState>();
	if (GS && GS->GetLoadoutAccess(Character->GetTeam()) < GetRequiredAccess())
	{
		return false;
	}
	return Team == DF::NoTeam || Character->GetTeam() == Team;
}

bool ADFLoadoutSource::IsInside(const ADFCharacter* Character) const
{
	return false;
}

// --- crate -----------------------------------------------------------------------------

ALoadoutCrate::ALoadoutCrate()
{
	Trigger = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Trigger"));
	Trigger->InitCapsuleSize(290.25f, 58.5f);   // MF LoadoutCrate CollisionRadius 129, CollisionHeight 26
	Trigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // tested by distance, no overlap events
	RootComponent = Trigger;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Trigger);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetMobility(EComponentMobility::Static);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		// MF crate decoration: CollisionRadius 40, CollisionHeight 12 uu
		Mesh->SetStaticMesh(Cube.Object);
		Mesh->SetRelativeScale3D(FVector(1.8f, 1.2f, 0.54f));
		Mesh->SetRelativeLocation(FVector(0.f, 0.f, -31.5f));
	}
	Trigger->SetMobility(EComponentMobility::Static);
}

bool ALoadoutCrate::IsInside(const ADFCharacter* Character) const
{
	// MF RagePlayer.TryLoadoutCrate: RadiusActors(LoadoutCrate, CollisionRadius)
	const FVector D = Character->GetActorLocation() - GetActorLocation();
	return D.Size2D() < Trigger->GetScaledCapsuleRadius() + 38.25f && FMath::Abs(D.Z) < Trigger->GetScaledCapsuleHalfHeight() + 87.75f;
}

void ALoadoutCrate::BeginPlay()
{
	Super::BeginPlay();
	if (!IsRelevantNow())
	{
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
	}
}

bool ALoadoutCrate::IsVisibleCrate() const
{
	// MF maps hide team crates on PlayerStarts (bHidden, bCollideActors=False): loadout trigger only
	return Variant != TEXT("Hidden");
}

void ALoadoutCrate::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateLook();
}

#if WITH_EDITOR
void ALoadoutCrate::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	UpdateLook();
}
#endif

void ALoadoutCrate::UpdateLook()
{
	if (Mesh)
	{
		Mesh->SetVisibility(IsVisibleCrate());
		Mesh->SetCollisionEnabled(IsVisibleCrate() ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
	// our crate (M_MF_Crate: generated wood, stencil in the team colour); flat team tint without it
	if (!Mesh || !Mesh->GetStaticMesh())
	{
		return;
	}
	const FLinearColor Color = Team == DF::NoTeam ? FLinearColor(0.9f, 0.7f, 0.15f) : UDFGameSettings::Get().GetTeamColor(Team);
	UMaterialInterface* M = UDFGameSettings::Get().CrateMaterial.LoadSynchronous();
	if (!M)
	{
		M = UDFGameSettings::Get().GetTeamTintMaterial();
	}
	if (M)
	{
		Mesh->SetMaterial(0, M);
	}
	Mesh->SetCustomPrimitiveDataVector4(0, Color);
}

// --- zone ------------------------------------------------------------------------------

ALoadoutZone::ALoadoutZone()
{
	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	Volume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Volume->SetBoxExtent(FVector(400.f));
	Volume->SetMobility(EComponentMobility::Static);
	Volume->bHiddenInGame = true;
	RootComponent = Volume;
	bTeamPlay = true;
	bNonTeamPlay = false;  // MF LoadoutZone: team games only; deathmatch uses the DM crates inside it
}

bool ALoadoutZone::IsInside(const ADFCharacter* Character) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(Character->GetActorLocation());
	const FVector E = Volume->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= E.X && FMath::Abs(Local.Y) <= E.Y && FMath::Abs(Local.Z) <= E.Z;
}
