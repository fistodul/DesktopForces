#include "Loadout/MFLoadout.h"
#include "Game/MFGameState.h"
#include "Player/MFCharacter.h"
#include "Weapons/MFWeapons.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

TArray<TSubclassOf<AMFWeapon>> UMFLoadoutLibrary::DefaultItems()
{
	// LoadoutCrate.aWeaponsInStore order
	return {
		AMFWeapon_Pistol::StaticClass(),
		AMFWeapon_MachineGun::StaticClass(),
		AMFWeapon_Shotgun::StaticClass(),
		AMFWeapon_Rifle::StaticClass(),
		AMFWeapon_TripBombs::StaticClass(),
		AMFWeapon_RocketLauncher::StaticClass(),
		AMFWeapon_Grenades::StaticClass(),
		AMFWeapon_AdrenalineShot::StaticClass(),
		AMFWeapon_Armour::StaticClass(),
		AMFWeapon_HeavyMachineGun::StaticClass(),
	};
}

AMFLoadoutSource* UMFLoadoutLibrary::FindLoadoutSourceFor(const AMFCharacter* Character)
{
	if (!Character)
	{
		return nullptr;
	}
	for (TActorIterator<AMFLoadoutSource> It(Character->GetWorld()); It; ++It)
	{
		if (It->IsUsableBy(Character) && It->IsInside(Character))
		{
			return *It;
		}
	}
	return nullptr;
}

// --- source ----------------------------------------------------------------------------

AMFLoadoutSource::AMFLoadoutSource()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;   // static map actors exist on every machine already
	Items = UMFLoadoutLibrary::DefaultItems();
}

void AMFLoadoutSource::BeginPlay()
{
	Super::BeginPlay();
}

bool AMFLoadoutSource::IsRelevantNow() const
{
	const AMFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AMFGameState>() : nullptr;
	const bool bTeamGame = GS && GS->bTeamGame;
	return bTeamGame ? bTeamPlay : bNonTeamPlay;
}

bool AMFLoadoutSource::IsUsableBy(const AMFCharacter* Character) const
{
	if (!Character || !Character->IsAlive() || !IsRelevantNow())
	{
		return false;
	}
	return Team == EMFTeam::None || Character->GetTeam() == Team;
}

bool AMFLoadoutSource::IsInside(const AMFCharacter* Character) const
{
	return false;
}

// --- crate -----------------------------------------------------------------------------

AMFLoadoutCrate::AMFLoadoutCrate()
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

bool AMFLoadoutCrate::IsInside(const AMFCharacter* Character) const
{
	// MF RagePlayer.TryLoadoutCrate: RadiusActors(LoadoutCrate, CollisionRadius)
	const FVector D = Character->GetActorLocation() - GetActorLocation();
	return D.Size2D() < Trigger->GetScaledCapsuleRadius() + 38.25f && FMath::Abs(D.Z) < Trigger->GetScaledCapsuleHalfHeight() + 87.75f;
}

void AMFLoadoutCrate::BeginPlay()
{
	Super::BeginPlay();
	if (!IsRelevantNow())
	{
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
	}
}

void AMFLoadoutCrate::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateLook();
}

#if WITH_EDITOR
void AMFLoadoutCrate::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	UpdateLook();
}
#endif

void AMFLoadoutCrate::UpdateLook()
{
	// Team colour on the placeholder box until the MF crate meshes are converted
	if (!Mesh || !Mesh->GetStaticMesh())
	{
		return;
	}
	const FLinearColor Color = Team == EMFTeam::Blue ? FLinearColor(0.1f, 0.2f, 0.8f)
		: Team == EMFTeam::Red ? FLinearColor(0.8f, 0.1f, 0.1f) : FLinearColor(0.6f, 0.45f, 0.1f);
	Mesh->SetCustomPrimitiveDataVector4(0, Color);
}

// --- zone ------------------------------------------------------------------------------

AMFLoadoutZone::AMFLoadoutZone()
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

bool AMFLoadoutZone::IsInside(const AMFCharacter* Character) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(Character->GetActorLocation());
	const FVector E = Volume->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= E.X && FMath::Abs(Local.Y) <= E.Y && FMath::Abs(Local.Z) <= E.Z;
}
