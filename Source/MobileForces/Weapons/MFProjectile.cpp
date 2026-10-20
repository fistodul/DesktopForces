#include "Weapons/MFProjectile.h"
#include "Core/MFProps.h"
#include "Player/MFCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

AMFProjectile::AMFProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(20.f);
	InitialLifeSpan = 15.f;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(18.f);   // MF CollisionRadius 8 uu
	Collision->SetCollisionProfileName(TEXT("Projectile"));
	Collision->SetGenerateOverlapEvents(true);
	Collision->CanCharacterStepUpOn = ECB_No;
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->CastShadow = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		Mesh->SetStaticMesh(Sphere.Object);
		Mesh->SetRelativeScale3D(FVector(0.2f));
	}

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->bRotationFollowsVelocity = true;
	Movement->bAutoActivate = false;

	DamageType = UMFDamage_Explosion::StaticClass();
}

void AMFProjectile::BeginPlay()
{
	Super::BeginPlay();
	if (!PropMesh.IsEmpty())
	{
		MF::ApplyPropMesh(Mesh, *PropMesh);   // the MF model, life size; the sphere stays without it
	}
	Movement->OnProjectileStop.AddDynamic(this, &AMFProjectile::OnStop);
	Movement->OnProjectileBounce.AddDynamic(this, &AMFProjectile::OnBounce);
	Collision->OnComponentBeginOverlap.AddDynamic(this, &AMFProjectile::OnOverlap);
	if (AActor* Inst = GetInstigator())
	{
		Collision->IgnoreActorWhenMoving(Inst, true);
	}
	if (HasAuthority() && FuseTime > 0.f)
	{
		SetLifeSpan(FuseTime);
	}
	if (!ExplosionSound && !MFExplosionSound.IsEmpty() && GetNetMode() != NM_DedicatedServer)
	{
		FString Package, Name;
		if (MFExplosionSound.Split(TEXT("."), &Package, &Name))
		{
			ExplosionSound = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/MFImported/Sounds/%s/%s.%s"), *Package, *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
	}
}

void AMFProjectile::Launch(const FVector& Dir, float Speed, const FVector& InheritVelocity)
{
	const float Base = Speed > 0.f ? Speed : Movement->InitialSpeed;
	// MF: "consider only the component of velocity in the direction of travel"
	const float Total = Base + FVector::DotProduct(InheritVelocity, Dir);
	FVector Vel = Dir * Total;
	Vel.Z += LaunchUpVelocity;
	Movement->Velocity = Vel;
	Movement->Activate();
}

void AMFProjectile::LifeSpanExpired()
{
	if (HasAuthority() && FuseTime > 0.f && !bExploded)
	{
		Explode(GetActorLocation(), FVector::UpVector);
		return;
	}
	Super::LifeSpanExpired();
}

void AMFProjectile::OnStop(const FHitResult& Hit)
{
	if (HasAuthority() && bExplodeOnImpact)
	{
		Explode(Hit.ImpactPoint, Hit.ImpactNormal);
	}
}

void AMFProjectile::OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity)
{
	if (HasAuthority() && bExplodeOnImpact)
	{
		Explode(Hit.ImpactPoint, Hit.ImpactNormal);
	}
}

void AMFProjectile::OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	if (HasAuthority() && bExplodeOnImpact && Other && Other != GetInstigator() && Cast<APawn>(Other))
	{
		Explode(GetActorLocation(), -GetVelocity().GetSafeNormal());
	}
}

void AMFProjectile::Explode(const FVector& Location, const FVector& Normal)
{
	if (bExploded)
	{
		return;
	}
	bExploded = true;
	AController* InstigatorController = GetInstigatorController();
	if (DamageRadius > 0.f)
	{
		// MF HurtRadius: damage * (1 - dist / radius), linear
		TArray<AActor*> Ignore;
		UGameplayStatics::ApplyRadialDamageWithFalloff(this, Damage, 0.f, Location + Normal * 36.f, 0.f, DamageRadius, 1.f,
			DamageType, Ignore, this, InstigatorController, ECC_Visibility);
	}
	MulticastExplosionFX(Location);
	SetLifeSpan(0.2f);  // let the multicast go out
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	Movement->StopMovementImmediately();
}

void AMFProjectile::MulticastExplosionFX_Implementation(FVector_NetQuantize Location)
{
	if (GetNetMode() != NM_DedicatedServer && ExplosionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ExplosionSound, Location);
	}
	SetActorHiddenInGame(true);
}

// --- concrete projectiles --------------------------------------------------------------

AMFProjectile_Rocket::AMFProjectile_Rocket()
{
	Movement->InitialSpeed = 2700.f;          // MF Rocket.speed 1200 uu/s
	Movement->MaxSpeed = 7200.f;              // MF MaxSpeed 3200
	Movement->ProjectileGravityScale = 0.f;
	Damage = 135.f;
	DamageRadius = 900.f;                     // MF HurtRadius 400
	Momentum = 225000.f;                      // MF MomentumTransfer 100000
	InitialLifeSpan = 10.f;
	MFExplosionSound = TEXT("MiscSFX.Explode");
	PropMesh = TEXT("RageWeapons/RocketMesh");
}

AMFProjectile_Grenade::AMFProjectile_Grenade()
{
	Movement->InitialSpeed = 1350.f;          // MF: ThrowPower * 150 uu/s (overridden by the throw)
	Movement->MaxSpeed = 2250.f;              // MF MaxSpeed 1000
	Movement->ProjectileGravityScale = 1.f;
	Movement->bShouldBounce = true;
	Movement->Bounciness = 0.5f;              // MF HitWall: Velocity = 0.5 * reflected
	Movement->Friction = 0.5f;
	Movement->BounceVelocityStopSimulatingThreshold = 45.f; // MF: speed < 20 uu -> stop
	Movement->bRotationFollowsVelocity = false;
	FuseTime = 3.f;                           // MF Grenade1.FuseTime
	bExplodeOnImpact = false;
	LaunchUpVelocity = 472.5f;                // MF Velocity.z += 210
	Damage = 160.f;
	DamageRadius = 900.f;                     // MF HurtRadius 400
	Momentum = 112500.f;
	MFExplosionSound = TEXT("MiscSFX.Explode");
	PropMesh = TEXT("RageWeapons/GrenadesPickupCarryMesh");
}

AMFProjectile_ImpactGrenade::AMFProjectile_ImpactGrenade()
{
	FuseTime = 8.f;                           // MF Grenade2.FuseTime
	bExplodeOnImpact = true;
	Damage = 120.f;
	Momentum = 90000.f;
}

AMFProjectile_Knife::AMFProjectile_Knife()
{
	Movement->InitialSpeed = 1350.f;          // MF Knife_Thrown.speed 600
	Movement->MaxSpeed = 2250.f;
	Movement->ProjectileGravityScale = 1.f;
	Damage = 60.f;
	DamageRadius = 0.f;
	DamageType = UMFDamage_Knife::StaticClass();
	InitialLifeSpan = 6.f;                    // MF TimeBeforeReturn
	PropMesh = TEXT("RageWeapons/KnifePickupCarryMesh");
}

void AMFProjectile_Knife::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	LastHit = Hit;
}

void AMFProjectile_Knife::Explode(const FVector& Location, const FVector& Normal)
{
	if (bExploded)
	{
		return;
	}
	bExploded = true;
	// Direct hit only: 60 damage, or 250 'decapitated' to the head (Knife_Thrown.ProcessTouch)
	TArray<FOverlapResult> Overlaps;
	FCollisionShape Shape = FCollisionShape::MakeSphere(30.f);
	GetWorld()->OverlapMultiByChannel(Overlaps, Location, FQuat::Identity, ECC_Pawn, Shape);
	for (const FOverlapResult& O : Overlaps)
	{
		AMFCharacter* Victim = Cast<AMFCharacter>(O.GetActor());
		if (!Victim || Victim == GetInstigator())
		{
			continue;
		}
		const bool bHead = Victim->IsHeadshot(Location);
		const float D = bHead ? 250.f : Damage;
		TSubclassOf<UDamageType> T = bHead ? TSubclassOf<UDamageType>(UMFDamage_Headshot::StaticClass()) : TSubclassOf<UDamageType>(DamageType);
		FPointDamageEvent Ev(D, LastHit, GetVelocity().GetSafeNormal(), T);
		Victim->TakeDamage(D, Ev, GetInstigatorController(), this);
		break;
	}
	Destroy();
}
