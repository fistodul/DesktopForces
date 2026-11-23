#include "Weapons/DFShellCase.h"
#include "Core/DFContent.h"
#include "Core/DFProps.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Sound/SoundBase.h"

ADFShellCase::ADFShellCase() {
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);
	// a pea-sized sphere sweeps against the world (not soldiers or vehicles), the mesh rides on it
	USphereComponent* Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->InitSphereRadius(1.5f);
	Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	Sphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Sphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Sphere->SetCanEverAffectNavigation(false);
	RootComponent = Sphere;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Sphere);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetCanEverAffectNavigation(false);
	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->SetUpdatedComponent(Sphere);
	Movement->bShouldBounce = true;
	Movement->Bounciness = 0.5f; // Velocity = 0.5 * reflected
	Movement->Friction = 0.4f;
	Movement->ProjectileGravityScale = 1.f;
	Movement->bRotationFollowsVelocity = false;
	Movement->bSweepCollision = true;
	Movement->OnProjectileBounce.AddDynamic(this, &ADFShellCase::OnBounce);
	Movement->OnProjectileStop.AddDynamic(this, &ADFShellCase::OnStop);
	InitialLifeSpan = 3.f;
}

void ADFShellCase::Eject(UWorld* World, const FDFCasing& Casing, const FVector& Eye, const FRotator& View, const FVector& Port, const FVector& InheritVelocity, float Side) {
	const FRotationMatrix Axes(View);
	const FVector X = Axes.GetUnitAxis(EAxis::X), Y = Axes.GetUnitAxis(EAxis::Y), Z = Axes.GetUnitAxis(EAxis::Z);
	const FVector At = Eye + X * Port.X + Y * Port.Y + Z * Port.Z;
	const FVector& Min = Casing.VelocityMin;
	const FVector& R = Casing.VelocityRange;
	const FVector V = ((FMath::FRand() * R.X + Min.X) * X + Side * (FMath::FRand() * R.Y + Min.Y) * Y + (FMath::FRand() * R.Z + Min.Z) * Z) * Casing.Speed;
	Launch(World, Casing, At, View, V + InheritVelocity);
}

ADFShellCase* ADFShellCase::Launch(UWorld* World, const FDFCasing& Casing, const FVector& At, const FRotator& Rotation, const FVector& Velocity) {
	if (!World || World->GetNetMode() == NM_DedicatedServer || Casing.Mesh.IsEmpty())
		return nullptr;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADFShellCase* S = World->SpawnActor<ADFShellCase>(At, Rotation, SP);
	if (!S)
		return nullptr;
	if (!DF::ApplyPropMesh(S->Mesh, *Casing.Mesh, Casing.Scale)) {
		S->Destroy(); // no classic content: none
		return nullptr;
	}
	S->MaxBounces = Casing.MaxBounces;
	S->Tink = Casing.Sound.IsEmpty() ? nullptr : DF::LoadClassic<USoundBase>(Casing.Sound);
	S->Movement->Velocity = Velocity;
	S->Movement->UpdateComponentVelocity();
	S->SetActorRotation(FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f));
	return S;
}

void ADFShellCase::OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity) {
	// RageRifle_BulletCase.HitWall: a tink each bounce, still after three (vehWeap_Shell: two)
	if (++Bounces > MaxBounces) {
		Movement->StopMovementImmediately();
		return;
	}
	if (Tink)
		DF::PlaySoundRadius(this, Tink, GetActorLocation(), 0.5f, 600.f);
	SetActorRotation(FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f)));
}

void ADFShellCase::OnStop(const FHitResult& Hit) {
	// Landed: lie flat
	SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 90.f));
}
