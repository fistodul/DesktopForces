#include "Weapons/MFProjectile.h"
#include "Sound/SoundWave.h"
#include "DesktopForces.h"
#include "TimerManager.h"
#include "Core/MFContent.h"
#include "Weapons/MFWeapon_Knife.h"
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
		if (AActor* Ride = Inst->GetAttachParentActor())
		{
			Collision->IgnoreActorWhenMoving(Ride, true);   // fired by a passenger
		}
	}
	if (HasAuthority() && FuseTime > 0.f)
	{
		SetLifeSpan(FuseTime);
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		FString Package, Name;
		if (MFAmbientSound.Split(TEXT("."), &Package, &Name))
		{
			USoundBase* Loop = MF::LoadClassic<USoundBase>(FString::Printf(TEXT("Sounds/%s/%s"), *Package, *Name));
			if (USoundWave* Wave = Cast<USoundWave>(Loop))
			{
				Wave->bLooping = true;
			}
			if (Loop)
			{
				UGameplayStatics::SpawnSoundAttached(Loop, RootComponent, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
					true, 1.f, 1.f, 0.f, MF::GetAttenuation(1600.f));
			}
		}
		if (bTicking)
		{
			FTimerHandle Tick;
			GetWorldTimerManager().SetTimer(Tick, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (GetGameTimeSinceCreation() < 3.5f)
				{
					MF::PlaySoundRadius(this, MF::LoadClassic<USoundBase>(TEXT("Sounds/WeaponSFX_Grenades/Ticking")), GetActorLocation(), 1.f);
				}
			}), 1.f, true);
		}
	}
	if (!ExplosionSound && !MFExplosionSound.IsEmpty() && GetNetMode() != NM_DedicatedServer)
	{
		FString Package, Name;
		if (MFExplosionSound.Split(TEXT("."), &Package, &Name))
		{
			ExplosionSound = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/MobileForcesClassic/Sounds/%s/%s.%s"), *Package, *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
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
	MFAmbientSound = TEXT("WeaponSFX_RocketLauncher.RocketFly");
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
	bTicking = true;
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
	bTicking = false;                         // only Grenade1 ticks
	bExplodeOnImpact = true;
	Damage = 120.f;
	Momentum = 90000.f;
}

AMFProjectile_Knife::AMFProjectile_Knife()
{
	Movement->InitialSpeed = 1350.f;          // MF Knife_Thrown.speed 600
	Movement->MaxSpeed = 2250.f;
	Movement->ProjectileGravityScale = 1.f;   // PHYS_Falling
	Movement->bShouldBounce = true;           // bBounce
	Movement->Bounciness = 0.4f;              // Bounce: Velocity = 0.4 * reflected
	Movement->Friction = 0.4f;
	Movement->BounceVelocityStopSimulatingThreshold = 45.f;   // speed < 20 uu: lies still
	Collision->InitSphereRadius(9.f);         // CollisionRadius 4
	Damage = 60.f;
	DamageRadius = 0.f;
	bExplodeOnImpact = false;
	DamageType = UMFDamage_Knife::StaticClass();
	InitialLifeSpan = 0.f;                    // goes when it's back with its owner
	PropMesh = TEXT("RageWeapons/KnifePickupCarryMesh");
}

void AMFProjectile_Knife::BeginPlay()
{
	Super::BeginPlay();
	Movement->OnProjectileBounce.AddDynamic(this, &AMFProjectile_Knife::OnKnifeBounce);
	Movement->OnProjectileStop.AddDynamic(this, &AMFProjectile_Knife::OnKnifeStop);
	Collision->OnComponentBeginOverlap.AddDynamic(this, &AMFProjectile_Knife::OnKnifeOverlap);
	if (HasAuthority())
	{
		SetLifeSpan(60.f);   // lost in the void: back after a minute anyway (Destroyed returns it)
	}
}

void AMFProjectile_Knife::HitVictim(AActor* Other, const FHitResult& Hit)
{
	if (!Other || (Other == GetInstigator() && !bCanHitOwner))
	{
		return;
	}
	const AMFCharacter* Victim = Cast<AMFCharacter>(Other);
	const bool bHead = Victim && Victim->IsHeadshot(GetActorLocation());
	const float D = bHead ? 250.f : Damage;
	TSubclassOf<UDamageType> T = bHead ? TSubclassOf<UDamageType>(UMFDamage_Headshot::StaticClass()) : TSubclassOf<UDamageType>(DamageType);
	FHitResult H = Hit;
	if (!H.bBlockingHit)
	{
		H.ImpactPoint = GetActorLocation();
		H.Location = GetActorLocation();
	}
	FPointDamageEvent Ev(D, H, GetVelocity().GetSafeNormal(), T);
	Other->TakeDamage(D, Ev, GetInstigatorController(), this);
}

void AMFProjectile_Knife::OnKnifeOverlap(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	if (!HasAuthority() || !Other)
	{
		return;
	}
	if (bSafe)
	{
		if (Other == GetInstigator())
		{
			Return();   // Safe.ProcessTouch: the owner takes it back
		}
		return;
	}
	if (Cast<APawn>(Other) && (Other != GetInstigator() || bCanHitOwner))
	{
		HitVictim(Other, Sweep);
		Movement->Velocity = FVector::ZeroVector;   // stops in them and drops
		Movement->bShouldBounce = false;
	}
}

void AMFProjectile_Knife::OnKnifeBounce(const FHitResult& Hit, const FVector& ImpactVelocity)
{
	if (!HasAuthority() || bSafe)
	{
		return;
	}
	if (Cast<APawn>(Hit.GetActor()))
	{
		HitVictim(Hit.GetActor(), Hit);   // vehicles: a clang and a push
		return;
	}
	bCanHitOwner = true;
	if (AActor* Inst = GetInstigator())
	{
		Collision->IgnoreActorWhenMoving(Inst, false);
	}
	// HitWall: sticks in walls (and anything steep); off the floor it bounces (Landed -> Bounce)
	if (FMath::Abs(Hit.ImpactNormal.Z) < 0.7f)
	{
		if (USoundBase* S = MF::LoadClassic<USoundBase>(TEXT("Sounds/WeaponSFX_Knife/HitWall")))
		{
			UGameplayStatics::PlaySoundAtLocation(this, S, Hit.ImpactPoint);
		}
		SetActorRotation(ImpactVelocity.Rotation());
		BecomeSafe(Hit.GetComponent());
	}
}

void AMFProjectile_Knife::OnKnifeStop(const FHitResult& Hit)
{
	if (HasAuthority() && !bSafe)
	{
		BecomeSafe(nullptr);
	}
}

void AMFProjectile_Knife::BecomeSafe(USceneComponent* StickTo)
{
	// state Safe: 8 x 16 uu to touch, back to the owner after TimeBeforeReturn
	bSafe = true;
	UE_LOG(LogMF, Verbose, TEXT("%s lies at %s%s"), *GetName(), *GetActorLocation().ToCompactString(), StickTo ? TEXT(" (stuck)") : TEXT(""));
	Movement->StopMovementImmediately();
	Movement->ProjectileGravityScale = 0.f;
	Movement->Deactivate();
	if (StickTo && StickTo->Mobility == EComponentMobility::Movable)
	{
		AttachToComponent(StickTo, FAttachmentTransformRules::KeepWorldTransform);   // SetBase(Wall)
	}
	Collision->SetSphereRadius(27.f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	GetWorldTimerManager().SetTimer(ReturnTimer, this, &AMFProjectile_Knife::Return, ReturnTime, false);
	// already standing on it
	TArray<AActor*> Touching;
	Collision->UpdateOverlaps();
	Collision->GetOverlappingActors(Touching, APawn::StaticClass());
	if (Touching.Contains(GetInstigator()))
	{
		Return();
	}
}

void AMFProjectile_Knife::Return()
{
	if (AMFWeapon_Knife* K = OwnerKnife.Get())
	{
		K->KnifeReturned();
	}
	OwnerKnife = nullptr;
	Destroy();
}

void AMFProjectile_Knife::Destroyed()
{
	if (AMFWeapon_Knife* K = OwnerKnife.Get(); K && HasAuthority())
	{
		K->KnifeReturned();
	}
	Super::Destroyed();
}
