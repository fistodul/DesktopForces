#include "World/WoodenBox.h"
#include "Core/DFContent.h"
#include "Core/DFProps.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

namespace {
	const TCHAR* WoodMaterial = TEXT("Materials/BoxPhysics/Skins/MI_Wood_opaque");

	/** The wood skin on every slot (MF left the box's second slot and the planks' one unset). */
	void ApplyWood(UStaticMeshComponent* Mesh) {
		if (UMaterialInterface* Wood = DF::LoadClassic<UMaterialInterface>(WoodMaterial))
			for (int32 I = 0; I < Mesh->GetNumMaterials(); ++I)
				Mesh->SetMaterial(I, Wood);
	}
} // namespace

AWoodenBox::AWoodenBox() {
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f; // only the floating
	bReplicates = true;
	SetReplicatingMovement(true);

	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->InitBoxExtent(FVector(90.f)); // the mesh at DrawScale 50: 80 uu across
	Collision->SetCollisionProfileName(TEXT("PhysicsActor"));
	Collision->SetSimulatePhysics(true);
	Collision->BodyInstance.SetMassOverride(100.f); // Mass 100
	Collision->SetShouldUpdatePhysicsVolume(true);
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AWoodenBox::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWoodenBox, bBroken);
}

void AWoodenBox::BeginPlay() {
	Super::BeginPlay();
	DF::ApplyPropMesh(Mesh, TEXT("BoxPhysics/Box"), 50.f);
	ApplyWood(Mesh);
	Buoyancy = 0.9f + 0.6f * FMath::FRand(); // some boxes float (randomly)
}

void AWoodenBox::Tick(float DeltaSeconds) {
	Super::Tick(DeltaSeconds);
	const APhysicsVolume* Volume = GetPhysicsVolume();
	if (!bBroken && Volume && Volume->bWaterVolume && Collision->IsSimulatingPhysics()) {
		// UE1 water: Buoyancy against gravity, and the water slows it
		Collision->AddForce(FVector(0.f, 0.f, -GetWorld()->GetGravityZ() * Buoyancy), NAME_None, true);
		Collision->SetPhysicsLinearVelocity(Collision->GetPhysicsLinearVelocity() * FMath::Max(0.f, 1.f - DeltaSeconds));
	}
}

float AWoodenBox::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) {
	// the push comes with the hit (shots and blasts move a physics body by themselves)
	Damage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
	if (!HasAuthority() || bBroken || Damage <= 0.f)
		return 0.f;
	Health -= Damage;
	if (Health < 0.f) {
		bBroken = true;
		OnRep_Broken();
		SetLifeSpan(1.f); // long enough for clients to hear it broke
	}
	return Damage;
}

void AWoodenBox::OnRep_Broken() {
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	Collision->SetSimulatePhysics(false);
	AWoodFragment::Burst(GetWorld(), GetActorLocation(), FragChunks, FragSize);
}

void AWoodFragment::Burst(UWorld* World, const FVector& Location, int32 Count, float Size) {
	if (!World || World->GetNetMode() == NM_DedicatedServer)
		return;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 I = 0; I < Count; ++I) {
		const FRotator Rot(FMath::FRand() * 360.f, FMath::FRand() * 360.f, 0.f);
		if (AWoodFragment* Frag = World->SpawnActor<AWoodFragment>(Location, Rot, SP)) {
			// Fragment.CalcVelocity: VRand() * (FRand() * 150 + 100) uu/s; DrawScale DSize * (0.5 + 0.7 * FRand())
			Frag->Movement->Velocity = FMath::VRand() * (FMath::FRand() * 150.f + 100.f) * 2.25f;
			Frag->Mesh->SetRelativeScale3D(FVector(Size * (0.5f + 0.7f * FMath::FRand())));
			Frag->SetLifeSpan(20.f + 40.f * FMath::FRand());
		}
	}
}

void AWoodFragment::PickMesh() {
	DF::ApplyPropMesh(Mesh, FMath::RandBool() ? TEXT("BoxPhysics/WoodFrag") : TEXT("BoxPhysics/WoodFrag2"));
	ApplyWood(Mesh);
}
