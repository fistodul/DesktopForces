#include "Weapons/TripBombs/TripBomb.h"
#include "Core/DFProps.h"
#include "Player/DFCharacter.h"
#include "Core/DFContent.h"
#include "World/DFDecals.h"
#include "Core/DFGameSettings.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ATripBomb::ATripBomb() {
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(5.f);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	RootComponent = Body;
	Model = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Model"));
	Model->SetupAttachment(Body);
	Model->SetUsingAbsoluteScale(true); // not the hit box's
	Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beam = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Beam"));
	Beam->SetupAttachment(Body);
	Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beam->CastShadow = false;
	Beam->SetVisibility(false);
	if (Cube.Succeeded()) {
		Body->SetStaticMesh(Cube.Object);
		Body->SetWorldScale3D(FVector(0.08f, 0.2f, 0.2f)); // MF CollisionRadius 8 uu
		Beam->SetStaticMesh(Cube.Object);
	}
}

void ATripBomb::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATripBomb, LaserEnd);
	DOREPLIFETIME(ATripBomb, Team);
}

bool ATripBomb::IsFriendlyTo(const AActor* Other) const {
	return DF::SideOf(Other, this, Team, PlacerState.Get()) == DF::ESide::Friend;
}

void ATripBomb::BeginPlay() {
	Super::BeginPlay();
	// TripBomb: Mesh TripBombMesh, its back on the wall (we stand 6 cm off it)
	if (DF::ApplyPropMesh(Model, TEXT("RageWeapons/TripBombMesh"))) {
		Model->SetWorldLocation(GetActorLocation() - GetActorForwardVector() * 6.f); // not scaled by the hit box
		Body->SetVisibility(false, false);
	}
	Shooter = GetInstigatorController(); // the planter may be dead when it goes off
	PlacerState = Shooter.IsValid() ? Shooter->PlayerState : nullptr;
	if (HasAuthority())
		GetWorldTimerManager().SetTimer(Timer, this, &ATripBomb::Arm, 1.f, false); // MF: SetTimer(1)
}

void ATripBomb::Arm() {
	const FVector Start = GetActorLocation();
	const FVector End = Start + GetActorForwardVector() * MaxLaserDistance;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFTripArm), false, this);
	LaserEnd = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) ? Hit.ImpactPoint : End;
	UpdateBeam();
	GetWorldTimerManager().SetTimer(Timer, this, &ATripBomb::CheckBeam, 0.05f, true);
}

void ATripBomb::OnRep_Laser() {
	UpdateBeam();
}

void ATripBomb::UpdateBeam() {
	const FVector Start = GetActorLocation();
	const FVector Delta = FVector(LaserEnd) - Start;
	const float Len = Delta.Size();
	if (Len < 1.f)
		return;
	// thin stretched cube in the planter's team colour (MF Blue/RedBeamRifle)
	DF::Tint(Beam, Team == DF::NoTeam ? FLinearColor(1.f, 0.2f, 0.1f) : UDFGameSettings::Get().GetTeamColor(Team));
	Beam->SetVisibility(true);
	Beam->SetWorldLocationAndRotation(Start + Delta * 0.5f, Delta.Rotation());
	Beam->SetWorldScale3D(FVector(Len / 100.f, 0.01f, 0.01f));
}

void ATripBomb::CheckBeam() {
	const FVector Start = GetActorLocation();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFTripBeam), false, this);
	const FCollisionObjectQueryParams Objects = DF::SolidObjects();
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Start, FVector(LaserEnd), Objects, Params) &&
		FVector::DistSquared(Hit.ImpactPoint, FVector(LaserEnd)) > 4.f * 4.f)
		Detonate(Hit.ImpactPoint, 200.f); // MF: HurtRadius(200, 400) at the beam break
}

float ATripBomb::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) {
	if (HasAuthority() && Damage > 0.f)
		Detonate(GetActorLocation(), 120.f);
	return Damage;
}

void ATripBomb::Detonate(const FVector& At, float Damage) {
	if (bExploded)
		return;
	bExploded = true;
	GetWorldTimerManager().ClearTimer(Timer);
	TArray<AActor*> Ignore;
	UGameplayStatics::ApplyRadialDamageWithFalloff(this, Damage, 0.f, At, 0.f, 900.f, 1.f,
		UDFDamage_Explosion::StaticClass(), Ignore, this, DF::PlacerCredit(Shooter.Get(), Team), ECC_Visibility);
	MulticastExplodeFX(GetActorLocation());
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetLifeSpan(0.2f); // let the multicast go out
}

void ATripBomb::MulticastExplodeFX_Implementation(FVector_NetQuantize At) {
	// TripBomb.Explode: EffectsFix.TripBombVehProx -> a RageEffects.Explosion (DrawScale 0.4), its bang
	if (GetNetMode() != NM_DedicatedServer) {
		DF::PlaySoundAtLocation(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/MiscSFX/Explode")), At);
		DF::SpawnEffect(this, TEXT("Explosion"), At, 0.4f);
		DF::SpawnScorchBelow(this, At, 180.f);
	}
	SetActorHiddenInGame(true);
}

// --- thrown proximity mine -------------------------------------------------------------

ATripMine::ATripMine() {
	ClassicImpactSound = TEXT("WeaponSFX_TripBombs.Bounce"); // TripBombThrown.ImpactSound
	Movement->InitialSpeed = 1125.f; // MF ThrowPower * 125 (overridden by the throw)
	Movement->MaxSpeed = 2250.f;
	Movement->ProjectileGravityScale = 1.f;
	Movement->bShouldBounce = true;
	Movement->Bounciness = 0.3f;
	Movement->Friction = 0.8f;
	Movement->bRotationFollowsVelocity = false;
	TumbleSpin = FRotator(0.f, 274.66f, 0.f); // TripBombThrown: RotationRate.Yaw up to 50000 either way
	bExplodeOnImpact = false;
	FuseTime = 0.f;
	InitialLifeSpan = 0.f; // stays until triggered
	Damage = 120.f; // MF TripBombOnGround.BlowUp: HurtRadius(120, 200)
	DamageRadius = 450.f;
	Momentum = 112500.f;
	ClassicExplosionSound = TEXT("MiscSFX.Explode");
	ExplosionEffectScale = 0.8f; // TripBombOnGround: AddExplosion Size 2
	PropMesh = TEXT("RageWeapons/TripBombsThrowMesh");
}

void ATripMine::BeginPlay() {
	Super::BeginPlay();
	if (const ADFCharacter* Thrower = Cast<ADFCharacter>(GetInstigator())) {
		PlacedTeam = Thrower->GetTeam();
		PlacerState = Thrower->GetEffectivePlayerState();
	}
	if (HasAuthority()) {
		// Arm after a moment so the thrower can get away; then scan at 10 Hz
		GetWorldTimerManager().SetTimer(ProximityTimer, this, &ATripMine::CheckProximity, 0.1f, true, 1.5f);
	}
}

void ATripMine::OnStop(const FHitResult& Hit) {
	HitOtherMine(Hit.GetActor());
	if (bExploded)
		return;
	// lie on its base: TripBombsThrowMesh's thin side (its local Y, base at +Y, the body
	// towards -Y) along the ground's normal, keeping where it points; flush with the ground
	const FVector Normal = Hit.ImpactNormal.IsNearlyZero() ? FVector::UpVector : Hit.ImpactNormal.GetSafeNormal();
	const FQuat Now = GetActorQuat();
	SetActorRotation(FQuat::FindBetweenNormals(Now.RotateVector(-FVector::YAxisVector), Normal) * Now);
	SetActorLocation(Hit.ImpactPoint + Normal * 2.25f);
	Super::OnStop(Hit);
}

void ATripMine::OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity) {
	HitOtherMine(Hit.GetActor());
	Super::OnBounce(Hit, ImpactVelocity);
}

AController* ATripMine::GetBlastCredit() const {
	return DF::PlacerCredit(Shooter.Get(), PlacedTeam); // none once the thrower changed sides
}

void ATripMine::HitOtherMine(AActor* Other) {
	ATripMine* Mine = Cast<ATripMine>(Other);
	if (!HasAuthority() || !Mine || Mine == this)
		return;
	Mine->SetOff();
	SetOff();
}

void ATripMine::SetOff() {
	if (bExploded || GetWorldTimerManager().IsTimerActive(SetOffTimer))
		return;
	GetWorldTimerManager().ClearTimer(ProximityTimer);
	GetWorldTimerManager().SetTimer(SetOffTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { Explode(GetActorLocation(), FVector::UpVector); }), 0.15f, false);
}

float ATripMine::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) {
	if (HasAuthority() && DamageAmount > 0.f && DamageCauser != this)
		SetOff();
	return DamageAmount;
}

void ATripMine::CheckProximity() {
	TArray<FOverlapResult> Hits;
	GetWorld()->OverlapMultiByChannel(Hits, GetActorLocation(), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(TriggerRadius));
	for (const FOverlapResult& H : Hits) {
		if (ADFCharacter* C = Cast<ADFCharacter>(H.GetActor()); C && C->IsAlive() && ShouldTrigger(C)) {
			GetWorldTimerManager().ClearTimer(ProximityTimer);
			Trigger(C);
			return;
		}
	}
}

void ATripMine::Trigger(ADFCharacter* Who) {
	Explode(GetActorLocation(), FVector::UpVector);
}
