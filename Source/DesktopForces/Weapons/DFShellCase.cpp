#include "Weapons/DFShellCase.h"
#include "Core/DFContent.h"
#include "Core/DFProps.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Sound/SoundBase.h"

ADFShellCase::ADFShellCase()
{
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
	Movement->Bounciness = 0.5f;          // Velocity = 0.5 * reflected
	Movement->Friction = 0.4f;
	Movement->ProjectileGravityScale = 1.f;
	Movement->bRotationFollowsVelocity = false;
	Movement->bSweepCollision = true;
	Movement->OnProjectileBounce.AddDynamic(this, &ADFShellCase::OnBounce);
	Movement->OnProjectileStop.AddDynamic(this, &ADFShellCase::OnStop);
	InitialLifeSpan = 3.f;
}

void ADFShellCase::Eject(UWorld* World, const FString& MeshName, float Scale, const FVector& Eye, const FRotator& View, const FVector& InheritVelocity)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer || MeshName.IsEmpty())
	{
		return;
	}
	const FRotationMatrix Axes(View);
	const FVector X = Axes.GetUnitAxis(EAxis::X), Y = Axes.GetUnitAxis(EAxis::Y), Z = Axes.GetUnitAxis(EAxis::Z);
	// EjectBrass: in front of the eyes, a little right and down (30 uu ahead, ~7 uu right)
	const FVector At = Eye + X * 50.f + Y * 16.f - Z * 14.f;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADFShellCase* S = World->SpawnActor<ADFShellCase>(At, View, SP);
	if (!S)
	{
		return;
	}
	if (!DF::ApplyPropMesh(S->Mesh, *MeshName, Scale))
	{
		S->Destroy();   // no classic content: no cases
		return;
	}
	// Eject(((FRand()*0.3+0.4)*X + (FRand()*0.2+0.2)*Y + (FRand()*0.3+1.0)*Z) * 160) uu/s
	const FVector V = ((FMath::FRand() * 0.3f + 0.4f) * X + (FMath::FRand() * 0.2f + 0.2f) * Y + (FMath::FRand() * 0.3f + 1.f) * Z) * 360.f;
	S->Movement->Velocity = V + InheritVelocity;
	S->Movement->UpdateComponentVelocity();
	S->SetActorRotation(FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f));
}

void ADFShellCase::OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity)
{
	// RageRifle_BulletCase.HitWall: a tink each bounce, gone after three
	if (++Bounces > 3)
	{
		Movement->StopMovementImmediately();
		return;
	}
	DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/MiscSFX/Shell")), GetActorLocation(), 0.5f, 600.f);
	SetActorRotation(FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f)));
}

void ADFShellCase::OnStop(const FHitResult& Hit)
{
	// Landed: lie flat
	SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 90.f));
}
