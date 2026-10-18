#include "Weapons/MFTripBomb.h"
#include "Player/MFCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AMFTripBomb::AMFTripBomb()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(5.f);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	RootComponent = Body;
	Beam = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Beam"));
	Beam->SetupAttachment(Body);
	Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beam->CastShadow = false;
	Beam->SetVisibility(false);
	if (Cube.Succeeded())
	{
		Body->SetStaticMesh(Cube.Object);
		Body->SetWorldScale3D(FVector(0.08f, 0.2f, 0.2f));   // MF CollisionRadius 8 uu
		Beam->SetStaticMesh(Cube.Object);
	}
}

void AMFTripBomb::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFTripBomb, LaserEnd);
}

void AMFTripBomb::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(Timer, this, &AMFTripBomb::Arm, 1.f, false);   // MF: SetTimer(1)
	}
}

void AMFTripBomb::Arm()
{
	const FVector Start = GetActorLocation();
	const FVector End = Start + GetActorForwardVector() * MaxLaserDistance;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFTripArm), false, this);
	LaserEnd = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) ? Hit.ImpactPoint : End;
	UpdateBeam();
	GetWorldTimerManager().SetTimer(Timer, this, &AMFTripBomb::CheckBeam, 0.05f, true);
}

void AMFTripBomb::OnRep_Laser()
{
	UpdateBeam();
}

void AMFTripBomb::UpdateBeam()
{
	const FVector Start = GetActorLocation();
	const FVector Delta = FVector(LaserEnd) - Start;
	const float Len = Delta.Size();
	if (Len < 1.f)
	{
		return;
	}
	// Thin stretched cube as the beam (team colour once materials exist)
	Beam->SetVisibility(true);
	Beam->SetWorldLocationAndRotation(Start + Delta * 0.5f, Delta.Rotation());
	Beam->SetWorldScale3D(FVector(Len / 100.f, 0.01f, 0.01f));
}

void AMFTripBomb::CheckBeam()
{
	const FVector Start = GetActorLocation();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFTripBeam), false, this);
	FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllObjects);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Start, FVector(LaserEnd), Objects, Params) &&
		FVector::DistSquared(Hit.ImpactPoint, FVector(LaserEnd)) > 4.f * 4.f)
	{
		Detonate(Hit.ImpactPoint, 200.f);   // MF: HurtRadius(200, 400) at the beam break
	}
}

float AMFTripBomb::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (HasAuthority() && Damage > 0.f)
	{
		Detonate(GetActorLocation(), 120.f);
	}
	return Damage;
}

void AMFTripBomb::Detonate(const FVector& At, float Damage)
{
	if (bExploded)
	{
		return;
	}
	bExploded = true;
	GetWorldTimerManager().ClearTimer(Timer);
	TArray<AActor*> Ignore;
	UGameplayStatics::ApplyRadialDamageWithFalloff(this, Damage, 0.f, At, 0.f, 900.f, 1.f,
		UMFDamage_Explosion::StaticClass(), Ignore, this, GetInstigatorController(), ECC_Visibility);
	Destroy();
}

// --- thrown proximity mine -------------------------------------------------------------

AMFProjectile_TripMine::AMFProjectile_TripMine()
{
	Movement->InitialSpeed = 1125.f;          // MF ThrowPower * 125 (overridden by the throw)
	Movement->MaxSpeed = 2250.f;
	Movement->ProjectileGravityScale = 1.f;
	Movement->bShouldBounce = true;
	Movement->Bounciness = 0.3f;
	Movement->Friction = 0.8f;
	Movement->bRotationFollowsVelocity = false;
	bExplodeOnImpact = false;
	FuseTime = 0.f;
	InitialLifeSpan = 0.f;                    // stays until triggered
	Damage = 120.f;                           // MF TripBombOnGround.BlowUp: HurtRadius(120, 200)
	DamageRadius = 450.f;
	Momentum = 112500.f;
	MFExplosionSound = TEXT("MiscSFX.Explode");
}

void AMFProjectile_TripMine::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		// Arm after a moment so the thrower can get away; then scan at 10 Hz
		GetWorldTimerManager().SetTimer(ProximityTimer, this, &AMFProjectile_TripMine::CheckProximity, 0.1f, true, 1.5f);
	}
}

void AMFProjectile_TripMine::CheckProximity()
{
	TArray<FOverlapResult> Hits;
	GetWorld()->OverlapMultiByChannel(Hits, GetActorLocation(), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(TriggerRadius));
	for (const FOverlapResult& H : Hits)
	{
		if (const AMFCharacter* C = Cast<AMFCharacter>(H.GetActor()); C && C->IsAlive())
		{
			GetWorldTimerManager().ClearTimer(ProximityTimer);
			Explode(GetActorLocation(), FVector::UpVector);
			return;
		}
	}
}
