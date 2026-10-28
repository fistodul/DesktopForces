#include "Weapons/DFProjectile.h"
#include "Sound/SoundWave.h"
#include "Particles/ParticleSystemComponent.h"
#include "DesktopForces.h"
#include "TimerManager.h"
#include "Core/DFContent.h"
#include "World/DFDecals.h"
#include "Weapons/Knife/Knife.h"
#include "Core/DFProps.h"
#include "Player/DFCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

ADFProjectile::ADFProjectile() {
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(20.f);
	InitialLifeSpan = 15.f;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(18.f); // MF CollisionRadius 8 uu
	Collision->SetCollisionProfileName(TEXT("Projectile"));
	Collision->SetGenerateOverlapEvents(true);
	Collision->CanCharacterStepUpOn = ECB_No;
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->CastShadow = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded()) {
		Mesh->SetStaticMesh(Sphere.Object);
		Mesh->SetRelativeScale3D(FVector(0.2f));
	}

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->bRotationFollowsVelocity = true;
	Movement->bAutoActivate = false;

	DamageType = UDFDamage_Explosion::StaticClass();
}

void ADFProjectile::BeginPlay() {
	Super::BeginPlay();
	if (!PropMesh.IsEmpty())
		DF::ApplyPropMesh(Mesh, *PropMesh); // the MF model, life size; the sphere stays without it
	Movement->OnProjectileStop.AddDynamic(this, &ADFProjectile::OnStop);
	Movement->OnProjectileBounce.AddDynamic(this, &ADFProjectile::OnBounce);
	Collision->OnComponentBeginOverlap.AddDynamic(this, &ADFProjectile::OnOverlap);
	if (AActor* Inst = GetInstigator()) {
		Collision->IgnoreActorWhenMoving(Inst, true);
		if (AActor* Ride = Inst->GetAttachParentActor())
			Collision->IgnoreActorWhenMoving(Ride, true); // fired by a passenger
	}
	if (HasAuthority() && FuseTime > 0.f)
		SetLifeSpan(FuseTime);
	if (GetNetMode() != NM_DedicatedServer) {
		DF::PlayLoop(RootComponent, ClassicAmbientSound);
		if (!TrailEffect.IsEmpty())
			Trail = DF::SpawnEffectAttached(*TrailEffect, RootComponent, 1.f, true);
		if (bTicking) {
			FTimerHandle Tick;
			GetWorldTimerManager().SetTimer(Tick, FTimerDelegate::CreateWeakLambda(this, [this]() {
				if (GetGameTimeSinceCreation() < 3.5f)
					DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/WeaponSFX_Grenades/Ticking")), GetActorLocation(), 1.f);
			}),
				1.f, true);
		}
	}
	if (!ExplosionSound && !ClassicExplosionSound.IsEmpty() && GetNetMode() != NM_DedicatedServer)
		ExplosionSound = DF::LoadSound(ClassicExplosionSound);
}

void ADFProjectile::Launch(const FVector& Dir, float Speed, const FVector& InheritVelocity) {
	const float Base = Speed > 0.f ? Speed : Movement->InitialSpeed;
	// MF: "consider only the component of velocity in the direction of travel"
	const float Total = Base + FVector::DotProduct(InheritVelocity, Dir);
	FVector Vel = Dir * Total;
	Vel.Z += LaunchUpVelocity;
	Movement->Velocity = Vel;
	Movement->Activate();
}

void ADFProjectile::LifeSpanExpired() {
	if (HasAuthority() && FuseTime > 0.f && !bExploded) {
		Explode(GetActorLocation(), FVector::UpVector);
		return;
	}
	Super::LifeSpanExpired();
}

void ADFProjectile::OnStop(const FHitResult& Hit) {
	if (HasAuthority() && bExplodeOnImpact)
		Explode(Hit.ImpactPoint, Hit.ImpactNormal);
}

void ADFProjectile::OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity) {
	if (HasAuthority() && bExplodeOnImpact)
		Explode(Hit.ImpactPoint, Hit.ImpactNormal);
}

void ADFProjectile::OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep) {
	if (HasAuthority() && bExplodeOnImpact && Other && Other != GetInstigator() && Cast<APawn>(Other))
		Explode(GetActorLocation(), -GetVelocity().GetSafeNormal());
}

void ADFProjectile::Explode(const FVector& Location, const FVector& Normal) {
	if (bExploded)
		return;
	bExploded = true;
	AController* InstigatorController = GetInstigatorController();
	if (DamageRadius > 0.f) {
		// MF HurtRadius: damage * (1 - dist / radius), linear
		TArray<AActor*> Ignore;
		UGameplayStatics::ApplyRadialDamageWithFalloff(this, Damage, 0.f, Location + Normal * 36.f, 0.f, DamageRadius, 1.f,
			DamageType, Ignore, this, InstigatorController, ECC_Visibility);
	}
	MulticastExplosionFX(Location + Normal * 36.f); // Rocket: AddExplosion(Location + HitNormal * 16)
	SetLifeSpan(0.2f); // let the multicast go out
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	Movement->StopMovementImmediately();
}

void ADFProjectile::MulticastExplosionFX_Implementation(FVector_NetQuantize Location) {
	if (Trail)
		Trail->DeactivateSystem();
	if (GetNetMode() != NM_DedicatedServer && ExplosionSound)
		DF::PlaySoundAtLocation(this, ExplosionSound, Location);
	if (ExplosionEffectScale > 0.f) {
		DF::SpawnEffect(this, TEXT("Explosion"), Location, ExplosionEffectScale);
		DF::SpawnScorchBelow(this, Location, ExplosionEffectScale * 450.f); // grenade ~1.8 m, rocket ~5.4 m
	}
	SetActorHiddenInGame(true);
}

// --- concrete projectiles --------------------------------------------------------------

ARocket::ARocket() {
	Movement->InitialSpeed = 2700.f; // MF Rocket.speed 1200 uu/s
	Movement->MaxSpeed = 7200.f; // MF MaxSpeed 3200
	Movement->ProjectileGravityScale = 0.f;
	Damage = 135.f;
	DamageRadius = 900.f; // MF HurtRadius 400
	Momentum = 225000.f; // MF MomentumTransfer 100000
	InitialLifeSpan = 10.f;
	ClassicExplosionSound = TEXT("MiscSFX.Explode");
	ExplosionEffectScale = 1.2f; // AddExplosion Size 3
	ClassicAmbientSound = TEXT("WeaponSFX_RocketLauncher.RocketFly");
	PropMesh = TEXT("RageWeapons/RocketMesh");
	TrailEffect = TEXT("RocketTrail");
}

AGrenade::AGrenade() {
	Movement->InitialSpeed = 1350.f; // MF: ThrowPower * 150 uu/s (overridden by the throw)
	Movement->MaxSpeed = 2250.f; // MF MaxSpeed 1000
	Movement->ProjectileGravityScale = 1.f;
	Movement->bShouldBounce = true;
	Movement->Bounciness = 0.5f; // MF HitWall: Velocity = 0.5 * reflected
	Movement->Friction = 0.5f;
	Movement->BounceVelocityStopSimulatingThreshold = 45.f; // MF: speed < 20 uu -> stop
	Movement->bRotationFollowsVelocity = false;
	FuseTime = 3.f; // MF Grenade1.FuseTime
	bTicking = true;
	bExplodeOnImpact = false;
	LaunchUpVelocity = 472.5f; // MF Velocity.z += 210
	Damage = 160.f;
	DamageRadius = 900.f; // MF HurtRadius 400
	Momentum = 112500.f;
	ClassicExplosionSound = TEXT("MiscSFX.Explode");
	ExplosionEffectScale = 0.4f; // AddExplosion Size 1
	PropMesh = TEXT("RageWeapons/GrenadesPickupCarryMesh");
}

AImpactGrenade::AImpactGrenade() {
	FuseTime = 8.f; // MF Grenade2.FuseTime
	bTicking = false; // only Grenade1 ticks
	bExplodeOnImpact = true;
	Damage = 120.f;
	Momentum = 90000.f;
}

AThrownKnife::AThrownKnife() {
	Movement->InitialSpeed = 3375.f; // RageKnife.ThrowKnife: ProjectileSpeed 10 * 150 uu/s
	Movement->MaxSpeed = 0.f; // (Knife_Thrown.MaxSpeed 1000 never applied: PHYS_Falling)
	Movement->ProjectileGravityScale = 1.f; // PHYS_Falling
	Movement->bShouldBounce = true; // bBounce
	Movement->Bounciness = 0.4f; // Bounce: Velocity = 0.4 * reflected
	Movement->Friction = 0.4f;
	Movement->BounceVelocityStopSimulatingThreshold = 45.f; // speed < 20 uu: lies still
	Collision->InitSphereRadius(9.f); // CollisionRadius 4
	Damage = 60.f;
	DamageRadius = 0.f;
	bExplodeOnImpact = false;
	DamageType = UDFDamage_Knife::StaticClass();
	InitialLifeSpan = 0.f; // goes when it's back with its owner
	PropMesh = TEXT("RageWeapons/KnifePickupCarryMesh");
}

void AThrownKnife::BeginPlay() {
	Super::BeginPlay();
	Movement->OnProjectileBounce.AddDynamic(this, &AThrownKnife::OnKnifeBounce);
	Movement->OnProjectileStop.AddDynamic(this, &AThrownKnife::OnKnifeStop);
	Collision->OnComponentBeginOverlap.AddDynamic(this, &AThrownKnife::OnKnifeOverlap);
	if (HasAuthority())
		SetLifeSpan(60.f); // lost in the void: back after a minute anyway (Destroyed returns it)
}

void AThrownKnife::HitVictim(AActor* Other, const FHitResult& Hit) {
	if (!Other || (Other == GetInstigator() && !bCanHitOwner))
		return;
	const ADFCharacter* Victim = Cast<ADFCharacter>(Other);
	const bool bHead = Victim && Victim->IsHeadshot(GetActorLocation());
	const float D = bHead ? HeadDamage : Damage;
	TSubclassOf<UDamageType> T = bHead ? TSubclassOf<UDamageType>(UDFDamage_Headshot::StaticClass()) : TSubclassOf<UDamageType>(DamageType);
	FHitResult H = Hit;
	if (!H.bBlockingHit) {
		H.ImpactPoint = GetActorLocation();
		H.Location = GetActorLocation();
	}
	FPointDamageEvent Ev(D, H, GetVelocity().GetSafeNormal(), T);
	Other->TakeDamage(D, Ev, GetInstigatorController(), this);
}

void AThrownKnife::OnKnifeOverlap(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep) {
	if (!HasAuthority() || !Other)
		return;
	if (bSafe) {
		if (Other == GetInstigator())
			Return(); // Safe.ProcessTouch: the owner takes it back
		return;
	}
	if (Cast<APawn>(Other) && (Other != GetInstigator() || bCanHitOwner)) {
		HitVictim(Other, Sweep);
		Movement->Velocity = FVector::ZeroVector; // stops in them and drops
		Movement->bShouldBounce = false;
	}
}

void AThrownKnife::OnKnifeBounce(const FHitResult& Hit, const FVector& ImpactVelocity) {
	if (!HasAuthority() || bSafe)
		return;
	if (Cast<APawn>(Hit.GetActor())) {
		// Dangerous.ProcessTouch: hurt them (vehicles: a clang and a push), stop dead, drop
		HitVictim(Hit.GetActor(), Hit);
		BecomeSafe(nullptr);
		return;
	}
	bCanHitOwner = true;
	if (AActor* Inst = GetInstigator())
		Collision->IgnoreActorWhenMoving(Inst, false);
	// HitWall: sticks in anything that isn't a floor (walls, slopes, ceilings); only a floor
	// (UE1 Landed: a walkable surface) makes it bounce, damped, until it lies still
	if (Hit.ImpactNormal.Z < 0.7f) {
		if (USoundBase* S = DF::LoadClassic<USoundBase>(TEXT("Sounds/WeaponSFX_Knife/HitWall")))
			DF::PlaySoundAtLocation(this, S, Hit.ImpactPoint);
		SetActorRotation(ImpactVelocity.Rotation());
		BecomeSafe(Hit.GetComponent());
	}
}

void AThrownKnife::OnKnifeStop(const FHitResult& Hit) {
	if (HasAuthority() && !bSafe)
		BecomeSafe(nullptr);
}

void AThrownKnife::BecomeSafe(USceneComponent* StickTo) {
	// state Safe: 8 x 16 uu to touch, back to the owner after TimeBeforeReturn
	bSafe = true;
	UE_LOG(LogDF, Verbose, TEXT("%s lies at %s%s"), *GetName(), *GetActorLocation().ToCompactString(), StickTo ? TEXT(" (stuck)") : TEXT(""));
	Movement->StopMovementImmediately();
	Movement->ProjectileGravityScale = 0.f;
	Movement->Deactivate();
	if (StickTo && StickTo->Mobility == EComponentMobility::Movable)
		AttachToComponent(StickTo, FAttachmentTransformRules::KeepWorldTransform); // SetBase(Wall)
	Collision->SetSphereRadius(27.f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	GetWorldTimerManager().SetTimer(ReturnTimer, this, &AThrownKnife::Return, ReturnTime, false);
	// already standing on it
	TArray<AActor*> Touching;
	Collision->UpdateOverlaps();
	Collision->GetOverlappingActors(Touching, APawn::StaticClass());
	if (Touching.Contains(GetInstigator()))
		Return();
}

void AThrownKnife::Return() {
	if (AKnife* K = OwnerKnife.Get())
		K->KnifeReturned();
	OwnerKnife = nullptr;
	Destroy();
}

void AThrownKnife::Destroyed() {
	if (AKnife* K = OwnerKnife.Get(); K && HasAuthority())
		K->KnifeReturned();
	Super::Destroyed();
}

void ADFProjectile::HomeOn(AActor* Target, float Delay) {
	if (!Target || !Movement)
		return;
	HomingTarget = Target;
	LaunchDir = Movement->Velocity.GetSafeNormal();
	GetWorldTimerManager().SetTimer(HomingTimer, this, &ADFProjectile::UpdateHoming, 0.05f, true, Delay);
}

void ADFProjectile::UpdateHoming() {
	// SmartRocket.SeekingState: turn hard towards the target, only while it's in front of the
	// launch direction (it doesn't come round for another pass)
	const AActor* T = HomingTarget.Get();
	const bool bAhead = T && FVector::DotProduct((T->GetActorLocation() - GetActorLocation()).GetSafeNormal(), LaunchDir) > 0.f;
	Movement->bIsHomingProjectile = bAhead;
	Movement->HomingTargetComponent = bAhead ? T->GetRootComponent() : nullptr;
	Movement->HomingAccelerationMagnitude = 60000.f; // MF: half the speed towards it every smoke puff (~1000 deg/s)
	if (!T)
		GetWorldTimerManager().ClearTimer(HomingTimer);
}

void ADFProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	DF::StopEffect(Trail);
	Super::EndPlay(EndPlayReason);
}
