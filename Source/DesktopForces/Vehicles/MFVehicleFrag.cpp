#include "Vehicles/MFVehicleFrag.h"
#include "Core/MFContent.h"
#include "Core/MFProps.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "Engine/World.h"

AMFVehicleFrag::AMFVehicleFrag()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(8.f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);   // walls and floors only
	RootComponent = Collision;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->CastShadow = false;
	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->bShouldBounce = true;
	Movement->Bounciness = 0.5f;            // HitWall: Velocity = 0.5 * reflected
	Movement->Friction = 0.f;
	Movement->bRotationFollowsVelocity = false;
	Movement->BounceVelocityStopSimulatingThreshold = 0.f;   // OnBounce decides when it lies still
	Spin = CreateDefaultSubobject<URotatingMovementComponent>(TEXT("Spin"));
	InitialLifeSpan = 10.f;
}

void AMFVehicleFrag::BeginPlay()
{
	Super::BeginPlay();
	// PreBeginPlay: a random mesh, RandSpin(125000), LifeSpan 10 + FRand() * 7
	MF::ApplyPropMesh(Mesh, *FString::Printf(TEXT("RageEffects/carfrag%02d"), FMath::RandRange(1, 9)));
	const float Rate = 687.f;   // 125000 UE1 units / s in degrees
	Spin->RotationRate = FRotator(FMath::FRandRange(-Rate, Rate), FMath::FRandRange(-Rate, Rate), FMath::FRandRange(-Rate, Rate));
	SetLifeSpan(10.f + FMath::FRand() * 7.f);
	Movement->OnProjectileBounce.AddDynamic(this, &AMFVehicleFrag::OnBounce);
}

void AMFVehicleFrag::OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity)
{
	Spin->RotationRate *= 0.75f;
	const FVector V = Movement->Velocity;
	if (V.Z < 112.5f && Hit.ImpactNormal.Z > 0.7f)
	{
		// landed: lies there at a random yaw
		Movement->StopSimulating(Hit);
		Spin->RotationRate = FRotator::ZeroRotator;
		SetActorRotation(FRotator(0.f, FMath::FRand() * 360.f, 0.f));
		return;
	}
	const float Speed = V.Size() / 2.25f;   // uu / s
	if (Speed > 60.f)
	{
		static const TCHAR* Hits[] = { TEXT("Sounds/VehicleFragSFX/FragHit1"), TEXT("Sounds/VehicleFragSFX/FragHit2"), TEXT("Sounds/VehicleFragSFX/FragHit3") };
		const float S = FMath::Min(Speed, 260.f) - 60.f;
		MF::PlaySoundRadius(this, MF::LoadClassic<USoundBase>(Hits[FMath::RandRange(0, 2)]), GetActorLocation(),
			0.3f * S / 200.f + 0.2f + FMath::FRand() * 0.2f, 1000.f, 1.f + FMath::FRand() * 0.1f);
	}
}

void AMFVehicleFrag::Burst(UWorld* World, const FVector& Location)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 I = 0; I < 16; ++I)
	{
		// WheelPos = Location + (Rand(128) - 64, Rand(128) - 64, 48 + Rand(64)); Velocity = Force * 7
		const FVector At = Location + FVector(FMath::RandRange(-64, 63), FMath::RandRange(-64, 63), 48 + FMath::RandRange(0, 63)) * 2.25f;
		const FVector Force(FMath::RandRange(-64, 63), FMath::RandRange(-64, 63), 64 + FMath::RandRange(0, 63));
		if (AMFVehicleFrag* Frag = World->SpawnActor<AMFVehicleFrag>(At, FRotator(FMath::FRand() * 360.f, FMath::FRand() * 360.f, 0.f), SP))
		{
			Frag->Movement->Velocity = Force * 7.f * 2.25f;
		}
	}
}
