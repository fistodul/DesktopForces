#include "Vehicles/VehicleFrag.h"
#include "World/DFDecals.h"
#include "Core/DFContent.h"
#include "Core/DFProps.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "Engine/World.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystemComponent.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

AVehicleFrag::AVehicleFrag()
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

void AVehicleFrag::BeginPlay()
{
	Super::BeginPlay();
	// PreBeginPlay: a random mesh, RandSpin(125000), LifeSpan 10 + FRand() * 7
	PickMesh();
	const float Rate = 687.f;   // 125000 UE1 units / s in degrees
	Spin->RotationRate = FRotator(FMath::FRandRange(-Rate, Rate), FMath::FRandRange(-Rate, Rate), FMath::FRandRange(-Rate, Rate));
	SetLifeSpan(10.f + FMath::FRand() * 7.f);
	Movement->OnProjectileBounce.AddDynamic(this, &AVehicleFrag::OnBounce);
}

void AVehicleFrag::PickMesh()
{
	DF::ApplyPropMesh(Mesh, *FString::Printf(TEXT("RageEffects/carfrag%02d"), FMath::RandRange(1, 9)));
}

void AVehicleFrag::PlayHitSound(float Speed)
{
	if (Speed > 60.f)
	{
		static const TCHAR* Hits[] = { TEXT("Sounds/VehicleFragSFX/FragHit1"), TEXT("Sounds/VehicleFragSFX/FragHit2"), TEXT("Sounds/VehicleFragSFX/FragHit3") };
		const float S = FMath::Min(Speed, 260.f) - 60.f;
		DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(Hits[FMath::RandRange(0, 2)]), GetActorLocation(),
			0.3f * S / 200.f + 0.2f + FMath::FRand() * 0.2f, 1000.f, 1.f + FMath::FRand() * 0.1f);
	}
}

void AVehicleFrag::OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity)
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
	PlayHitSound(V.Size() / 2.25f);   // uu / s
}

void AVehicleFrag::Burst(UWorld* World, const FVector& Location)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	int32 TrailFXC = 4;   // "Max num of flamming bits"
	for (int32 I = 0; I < 16; ++I)
	{
		// WheelPos = Location + (Rand(128) - 64, Rand(128) - 64, 48 + Rand(64)); Velocity = Force * 7
		const FVector At = Location + FVector(FMath::RandRange(-64, 63), FMath::RandRange(-64, 63), 48 + FMath::RandRange(0, 63)) * 2.25f;
		const FVector Force(FMath::RandRange(-64, 63), FMath::RandRange(-64, 63), 64 + FMath::RandRange(0, 63));
		if (AVehicleFrag* Frag = World->SpawnActor<AVehicleFrag>(At, FRotator(FMath::FRand() * 360.f, FMath::FRand() * 360.f, 0.f), SP))
		{
			Frag->Movement->Velocity = Force * 7.f * 2.25f;
			if (TrailFXC > 0 && FMath::FRand() < 0.4f)
			{
				--TrailFXC;
				Fire(World, Frag->GetRootComponent(), At);
			}
		}
	}
}

void AVehicleFrag::Explosion(UWorld* World, const FVector& Location, float Scale)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	DF::SpawnEffect(World, TEXT("VehicleExplosion"), Location, Scale);
	DF::SpawnScorchBelow(World, Location, 500.f * Scale);
	// "Create some after fires": Location + VRand() * (100, 100, 0), on whatever is 80 uu below
	for (int32 I = 2 + FMath::RandRange(0, 2); I > 0; --I)
	{
		const FVector From = Location + FVector(FMath::VRand().GetSafeNormal2D() * FMath::FRand() * 225.f) * Scale;
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, From, From - FVector(0.f, 0.f, 180.f), ECC_WorldStatic))
		{
			Fire(World, nullptr, Hit.ImpactPoint, 1.f - FMath::FRand() * 0.3f);
		}
	}
}

void AVehicleFrag::Fire(UWorld* World, USceneComponent* AttachTo, const FVector& At, float Scale)
{
	UParticleSystemComponent* PS = AttachTo ? DF::SpawnEffectAttached(TEXT("ExplosionFire"), AttachTo, Scale) : DF::SpawnEffect(World, TEXT("ExplosionFire"), At, Scale);
	if (!PS)
	{
		return;
	}
	// AmbientSound VehicleBurnLoop at SoundVolume 64 while it burns (ScaleGlow 1 -> 0.1: 4.5 s)
	USoundBase* Loop = DF::LoadClassic<USoundBase>(TEXT("Sounds/VehicleFragSFX/VehicleBurnLoop"));
	UAudioComponent* Audio = !Loop ? nullptr : AttachTo
		? UGameplayStatics::SpawnSoundAttached(Loop, AttachTo, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, true, 0.25f)
		: UGameplayStatics::SpawnSoundAtLocation(World, Loop, At, FRotator::ZeroRotator, 0.25f);
	if (Audio)
	{
		FTimerHandle T;
		World->GetTimerManager().SetTimer(T, FTimerDelegate::CreateWeakLambda(Audio, [Audio]() { Audio->FadeOut(0.5f, 0.f); }), 4.5f, false);
	}
}
