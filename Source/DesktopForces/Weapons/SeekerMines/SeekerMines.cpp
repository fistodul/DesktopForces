#include "Weapons/SeekerMines/SeekerMines.h"
#include "DesktopForces.h"
#include "Core/DFProps.h"
#include "Core/DFContent.h"
#include "World/DFEffectActor.h"
#include "Core/DFGameSettings.h"
#include "Player/DFCharacter.h"
#include "Weapons/DFDevices.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "AIController.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "SeekerMines"

UDFDamage_SeekerMine::UDFDamage_SeekerMine() {
	bExplosive = true;
	VehicleDamageScale = 1.5f;
	DeathVerb = LOCTEXT("HuntedDown", "hunted down");
}

// --- the drone --------------------------------------------------------------------------

ASeekerMine::ASeekerMine() {
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicatingMovement(true);
	InitialLifeSpan = 480.f;
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::Spawned;

	// MF CollisionRadius 14 uu; touches players (no blocking) to go off
	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(31.5f);
	Collision->SetCollisionProfileName(TEXT("Pawn"));
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Collision->SetGenerateOverlapEvents(true);
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded()) {
		Mesh->SetStaticMesh(Sphere.Object); // placeholder until the mod's drone model is converted
		Mesh->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.3f));
	}

	// LightType LT_Pulse, LightRadius 12 (25 x 13 = 325 uu), LightBrightness 255
	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Collision);
	Glow->SetMobility(EComponentMobility::Movable);
	Glow->SetCastShadows(false);
	Glow->bUseInverseSquaredFalloff = false;
	Glow->LightFalloffExponent = 2.f;
	Glow->SetIntensityUnits(ELightUnits::Unitless);
	Glow->SetIntensity(3.f);
	Glow->SetAttenuationRadius(731.25f);
	Glow->SetLightColor(FLinearColor(1.f, 0.6f, 0.1f)); // hue 40: nobody's yet

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->MaxSpeed = 1395.f; // MF AirSpeed 620 uu/s
	Movement->Acceleration = 5400.f; // MF AccelRate 2400
	Movement->Deceleration = 5400.f;
	Movement->UpdatedComponent = Collision;
}

void ASeekerMine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASeekerMine, PlacedTeam);
}

void ASeekerMine::InitPlacer(ADFCharacter* Placer) {
	SetInstigator(Placer);
	Shooter = Placer ? Placer->GetController() : nullptr;
	PlacerState = Placer ? Placer->GetEffectivePlayerState() : nullptr; // in a vehicle too
	PlacedTeam = Placer ? Placer->GetTeam() : DF::NoTeam;
	OnRep_PlacedTeam();
}

void ASeekerMine::OnRep_PlacedTeam() {
	// InitPlacer: LightHue 170 for blue, 0 for red
	if (PlacedTeam != DF::NoTeam)
		Glow->SetLightColor(UDFGameSettings::Get().GetTeamColor(PlacedTeam));
}

void ASeekerMine::Pulse() {
	// LT_Pulse: the brightness swings with the light's period (LightPeriod 32: about a second)
	if (!WasRecentlyRendered(0.5f))
		return;
	const float Phase = FMath::Sin(float(GetWorld()->GetTimeSeconds()) * UE_TWO_PI);
	Glow->SetIntensity(3.f * (0.6f + 0.4f * Phase));
}

void ASeekerMine::BeginPlay() {
	Super::BeginPlay();
	BaseSpeed = Movement->MaxSpeed;
	// SeekerMine: LodMesh'SeekerMines.Drone_Air' (Marcin's model, fistodul's textures); the
	// team-tinted ball without the MF-mods content
	if (!DF::ApplyPropMesh(Mesh, TEXT("SeekerMines/Drone_Air")) && GetNetMode() != NM_DedicatedServer) {
		if (UMaterialInterface* M = UDFGameSettings::Get().GetTeamTintMaterial())
			Mesh->SetMaterial(0, M);
	}
	Collision->OnComponentBeginOverlap.AddDynamic(this, &ASeekerMine::OnOverlap);
	if (GetNetMode() != NM_DedicatedServer) {
		// AmbientSound at a Pawn's defaults (Engine.u: SoundRadius 9, SoundVolume 240, SoundPitch
		// 64): heard only within 225 uu, at full pitch
		if (UAudioComponent* Hum = DF::PlayLoop(Collision, TEXT("WeaponSFX_TripBombs.Active"), 225.f))
			Hum->SetVolumeMultiplier(240.f / 255.f);
		GetWorldTimerManager().SetTimer(PulseTimer, this, &ASeekerMine::Pulse, 0.1f, true);
	} else
		Glow->SetVisibility(false);
	if (HasAuthority()) {
		ADFEffectActor::SpawnSound(GetWorld(), TEXT("WeaponSFX_TripBombs.Activating"), GetActorLocation());
		// state Deploying: 0.35 s, then hunt
		GetWorldTimerManager().SetTimer(ThinkTimer, this, &ASeekerMine::Think, 0.2f, true, 0.35f);
	}
}

bool ASeekerMine::IsFriendly(const AActor* Other) const {
	return DF::SideOf(Other, this, PlacedTeam, PlacerState.Get()) != DF::ESide::Enemy;
}

AActor* ASeekerMine::FindBestTarget() const {
	// occupied enemy vehicles, then enemy soldiers, sentry turrets and seeker mines
	return DF::FindDeviceTarget(this, PlacedTeam, PlacerState.Get(), SeekRadius, [](const AActor*) { return true; });
}

void ASeekerMine::MoveTo(AActor* Goal, float Acceptance, float SpeedScale) {
	Movement->MaxSpeed = BaseSpeed * SpeedScale;
	AAIController* AI = Cast<AAIController>(GetController());
	if (!AI)
		return;
	// Hunting: actorReachable -> MoveToward straight through the air, else FindPathToward (the
	// navmesh here; a hovering drone off it used to find no path and sit still)
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFSeekerSight), false, this);
	Params.AddIgnoredActor(Goal);
	const bool bClear = !GetWorld()->SweepTestByChannel(GetActorLocation(), Goal->GetActorLocation(), FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(Collision->GetScaledSphereRadius()), Params);
	AI->MoveToActor(Goal, Acceptance, true, !bClear, true, nullptr, true);
}

void ASeekerMine::Think() {
	if (bDetonating)
		return;
	AAIController* AI = Cast<AAIController>(GetController());
	// state Hunting
	if (AActor* Target = FindBestTarget()) {
		if (Target != Hunted.Get() && (!Hunted.IsValid() || !DF::IsDeviceTargetAlive(Hunted.Get()) || IsFriendly(Hunted.Get()))) {
			// Hunting.Scan: a beep when it starts a hunt or its prey is gone, not per change of
			// the nearest (PlaySound(Activating, 1.5): a pawn's TransientSoundVolume 2 x 1.5)
			ADFEffectActor::SpawnSound(GetWorld(), TEXT("WeaponSFX_TripBombs.Activating"), GetActorLocation(), 1600.f, 1.f);
		}
		Hunted = Target;
		if (FVector::Dist(Target->GetActorLocation(), GetActorLocation()) <= DetonateRange) {
			StartDetonating();
			return;
		}
		MoveTo(Target, 50.f, 1.f);
		return;
	}
	Hunted = nullptr;
	// state Escorting: stay 250-450 uu from the owner while they're near (2500 uu)
	const ADFCharacter* Placer = Cast<ADFCharacter>(GetInstigator());
	const float PlacerDist = Placer ? FVector::Dist(Placer->GetActorLocation(), GetActorLocation()) : 1e9f;
	if (Placer && Placer->IsAlive() && PlacerDist < 5625.f) {
		Escort(Placer, PlacerDist);
		return;
	}
	// state Roaming: wander between random reachable spots at half speed
	const float Now = GetWorld()->GetTimeSeconds();
	if (AI && Now >= RoamUntil) {
		RoamUntil = Now + 4.f;
		FNavLocation Dest;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (Nav && Nav->GetRandomReachablePointInRadius(GetActorLocation(), 3000.f, Dest)) {
			Movement->MaxSpeed = BaseSpeed * 0.5f;
			AI->MoveToLocation(Dest.Location, 100.f);
		}
	}
}

void ASeekerMine::Escort(const ADFCharacter* Placer, float Dist) {
	AAIController* AI = Cast<AAIController>(GetController());
	if (!AI)
		return;
	// state Escorting, as the mod has it. Over 450 uu off it flies at the placer at 0.85 speed -
	// MoveToward, which only ends where it reaches them - then backs away 160 uu while within
	// 250, and in between drifts, its speed halving (Velocity * 0.5), facing them. Its swings
	// round the placer come from that: it overshoots and backs off as they move.
	if (Dist > 1012.5f)
		bEscortClosing = true;
	UE_LOG(LogDF, VeryVerbose, TEXT("%s escorting %s at %.0f cm%s"), *GetName(), *Placer->GetName(), Dist, bEscortClosing ? TEXT(", closing in") : TEXT(""));
	// "reaches them": in UE1 the cylinders touch; a hovering drone above or below the placer's
	// capsule never gets that close here, so it counts once inside the 250 uu it backs off from
	if (bEscortClosing && Dist > 562.5f) {
		MoveTo(const_cast<ADFCharacter*>(Placer), 300.f, 0.85f);
		return;
	}
	bEscortClosing = false;
	if (Dist < 562.5f) {
		Movement->Velocity *= 0.3f;
		Movement->MaxSpeed = BaseSpeed * 0.5f;
		const FVector Away = (GetActorLocation() - Placer->GetActorLocation()).GetSafeNormal2D();
		AI->MoveToLocation(GetActorLocation() + Away * 360.f, 30.f, false, false, false, false);
		return;
	}
	AI->StopMovement();
	Movement->Velocity *= 0.5f;
	SetActorRotation(FRotator(0.f, (Placer->GetActorLocation() - GetActorLocation()).Rotation().Yaw, 0.f)); // TurnToward
}

void ASeekerMine::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body, bool bSweep, const FHitResult& Hit) {
	// Touch: an enemy soldier, vehicle or device bumping into it (walls and props are "friendly")
	if (HasAuthority() && !IsFriendly(Other) && DF::IsDeviceTargetAlive(Other))
		StartDetonating();
}

void ASeekerMine::StartDetonating() {
	// state Detonating: a beep, 0.12 s, boom
	if (bDetonating || bDetonated)
		return;
	bDetonating = true;
	ADFEffectActor::SpawnSound(GetWorld(), TEXT("WeaponSFX_TripBombs.Activating"), GetActorLocation());
	GetWorldTimerManager().SetTimer(ThinkTimer, this, &ASeekerMine::BlowUp, 0.12f, false);
}

float ASeekerMine::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) {
	if (DF::IsRunOverByFriend(DamageEvent, EventInstigator, this, PlacedTeam, PlacerState.Get()))
		return 0.f;
	DamageAmount *= DF::RadialDamageScale(DamageEvent, GetActorLocation(), 0.f);
	if (HasAuthority() && !bDetonating && DamageAmount > 0.f) {
		Health -= FMath::CeilToInt(DamageAmount);
		if (Health <= 0)
			BlowUp();
	}
	return DamageAmount;
}

void ASeekerMine::BlowUp() {
	if (bDetonated)
		return;
	bDetonated = true;
	AController* Credit = DF::PlacerCredit(Shooter.Get(), PlacedTeam);
	const FVector At = GetActorLocation() + FVector(0.f, 0.f, 36.f);
	TArray<AActor*> Ignore{ this };
	UGameplayStatics::ApplyRadialDamageWithFalloff(this, Damage, 0.f, At, 0.f, DamageRadius, 1.f,
		UDFDamage_SeekerMine::StaticClass(), Ignore, this, Credit, ECC_Visibility);
	ADFEffectActor::Spawn(GetWorld(), TEXT("Explosion"), At, 1.4f); // AddExplosionServer(Size 3.5)
	ADFEffectActor::SpawnSound(GetWorld(), TEXT("WeaponSFX_TripBombs.Bounce"), At, 1600.f, 1.f); // PlaySound(Bounce, 2.0)
	MakeNoise(1.f, GetInstigator());
	Destroy();
}

// --- thrown -----------------------------------------------------------------------------------

ASeekerMineThrown::ASeekerMineThrown() {
	ClassicImpactSound = TEXT("WeaponSFX_TripBombs.Bounce"); // SeekerMineThrown extends TripBombThrown
	Movement->InitialSpeed = 2700.f; // MF speed 1200 uu/s (the throw sets it)
	Movement->MaxSpeed = 6750.f; // MF MaxSpeed 3000
	Movement->ProjectileGravityScale = 1.f;
	bExplodeOnImpact = true;
	FuseTime = 0.5f; // SetTimer(0.5) -> DeployMine
	Damage = 0.f;
	PropMesh = TEXT("SeekerMines/Drone_Air");
}

void ASeekerMineThrown::Explode(const FVector& Location, const FVector& Normal) {
	if (bExploded)
		return;
	bExploded = true;
	FActorSpawnParameters SP;
	SP.Instigator = GetInstigator();
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	if (ASeekerMine* Mine = GetWorld()->SpawnActor<ASeekerMine>(Location + Normal * 40.f, FRotator(0.f, GetActorRotation().Yaw, 0.f), SP))
		Mine->InitPlacer(Cast<ADFCharacter>(GetInstigator()));
	Destroy();
}

// --- the weapon -------------------------------------------------------------------------------

ASeekerMines::ASeekerMines() {
	DisplayName = LOCTEXT("SeekerMines", "Seeker Mine");
	MaxClipAmmo = 1;
	ThrowReleaseFloor = 2.f; // AltFiring.Tick: if (ThrowPower < 2) ThrowPower += 2
	AltProjectileClass = ASeekerMineThrown::StaticClass();
	bAddToLoadout = true;
	// Icon converted from the mod's own art (SourceArt/Mods/Icons/SeekerMineIcon.bmp)
	HUDIcon = TEXT("/Game/MF/Mods/Icons/SeekerMineIcon");
	HUDIconRect = FIntRect(0, 0, 64, 64);
}

void ASeekerMines::StartFire(bool bAlt) {
	if (!bWinding)
		HeldButtons = 0; // a hold left over from before a switch (its release went elsewhere)
	HeldButtons |= bAlt ? 2 : 1;
	Super::StartFire(true); // SeekerMines.Fire -> AltFire: both buttons wind up a throw
}

void ASeekerMines::StopFire(bool bAlt) {
	// AltFiring.Tick: the throw goes when bAltFire == 0 && bFire == 0, not on either release
	HeldButtons &= bAlt ? ~2 : ~1;
	if (HeldButtons == 0)
		Super::StopFire(true);
}

bool ASeekerMines::CanPlace() const {
	const ADFCharacter* Me = GetOwnerCharacter();
	int32 Active = 0;
	for (TActorIterator<ASeekerMine> It(GetWorld()); It; ++It)
		Active += It->GetInstigator() == Me && !It->IsActorBeingDestroyed();
	if (Active < MaxActiveMines)
		return true;
	if (APlayerController* PC = Me ? Cast<APlayerController>(Me->GetController()) : nullptr)
		PC->ClientMessage(FString::Printf(TEXT("Max active Seeker Mines reached: %d"), MaxActiveMines));
	return false;
}

#undef LOCTEXT_NAMESPACE
