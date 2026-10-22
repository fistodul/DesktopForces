#include "Weapons/SeekerMines/SeekerMines.h"
#include "World/DFEffectActor.h"
#include "Core/DFGameSettings.h"
#include "Player/DFCharacter.h"
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

namespace SeekerLocal
{
	USoundBase* Sound(const TCHAR* Path)
	{
		return LoadObject<USoundBase>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	const TCHAR* ActivatingSound = TEXT("/MobileForcesClassic/Sounds/WeaponSFX_TripBombs/Activating.Activating");
}

UDFDamage_SeekerMine::UDFDamage_SeekerMine()
{
	bExplosive = true;
	VehicleDamageScale = 1.5f;
	DeathVerb = LOCTEXT("HuntedDown", "hunted down");
}

// --- the drone --------------------------------------------------------------------------

ASeekerMine::ASeekerMine()
{
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
	if (Sphere.Succeeded())
	{
		Mesh->SetStaticMesh(Sphere.Object);   // placeholder until the mod's drone model is converted
		Mesh->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.3f));
	}

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->MaxSpeed = 1395.f;         // MF AirSpeed 620 uu/s
	Movement->Acceleration = 5400.f;     // MF AccelRate 2400
	Movement->Deceleration = 5400.f;
	Movement->UpdatedComponent = Collision;
}

void ASeekerMine::InitPlacer(ADFCharacter* Placer)
{
	SetInstigator(Placer);
	PlacedTeam = Placer ? Placer->GetTeam() : DF::NoTeam;
}

void ASeekerMine::BeginPlay()
{
	Super::BeginPlay();
	BaseSpeed = Movement->MaxSpeed;
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (UMaterialInterface* M = UDFGameSettings::Get().GetTeamTintMaterial())
		{
			Mesh->SetMaterial(0, M);
		}
	}
	Collision->OnComponentBeginOverlap.AddDynamic(this, &ASeekerMine::OnOverlap);
	if (HasAuthority())
	{
		UGameplayStatics::PlaySoundAtLocation(this, SeekerLocal::Sound(SeekerLocal::ActivatingSound), GetActorLocation());
		// state Deploying: 0.35 s, then hunt
		GetWorldTimerManager().SetTimer(ThinkTimer, this, &ASeekerMine::Think, 0.2f, true, 0.35f);
	}
}

bool ASeekerMine::IsFriendly(const AActor* Other) const
{
	const ADFCharacter* C = Cast<ADFCharacter>(Other);
	if (!C || !C->IsAlive())
	{
		return true;   // only living players are worth chasing
	}
	return C == GetInstigator() || (PlacedTeam != DF::NoTeam && C->GetTeam() == PlacedTeam);
}

ADFCharacter* ASeekerMine::FindBestTarget() const
{
	ADFCharacter* Best = nullptr;
	float BestDist = SeekRadius;
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It)
	{
		const float D = FVector::Dist(It->GetActorLocation(), GetActorLocation());
		if (D < BestDist && !IsFriendly(*It))
		{
			BestDist = D;
			Best = *It;
		}
	}
	return Best;
}

void ASeekerMine::MoveTo(AActor* Goal, float Acceptance, float SpeedScale)
{
	Movement->MaxSpeed = BaseSpeed * SpeedScale;
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->MoveToActor(Goal, Acceptance, true, true, true, nullptr, true);
	}
}

void ASeekerMine::Think()
{
	if (bDetonating)
	{
		return;
	}
	AAIController* AI = Cast<AAIController>(GetController());
	// state Hunting
	if (ADFCharacter* Target = FindBestTarget())
	{
		if (FVector::Dist(Target->GetActorLocation(), GetActorLocation()) <= DetonateRange)
		{
			StartDetonating();
			return;
		}
		MoveTo(Target, 50.f, 1.f);
		return;
	}
	// state Escorting: stay 250-450 uu from the owner while they're near
	const ADFCharacter* Owner = Cast<ADFCharacter>(GetInstigator());
	const float OwnerDist = Owner ? FVector::Dist(Owner->GetActorLocation(), GetActorLocation()) : 1e9f;
	if (Owner && Owner->IsAlive() && OwnerDist < 5625.f)
	{
		if (OwnerDist > 1012.f)
		{
			MoveTo(const_cast<ADFCharacter*>(Owner), 600.f, 0.85f);
		}
		else if (AI)
		{
			AI->StopMovement();
		}
		return;
	}
	// state Roaming: wander between random reachable spots at half speed
	const float Now = GetWorld()->GetTimeSeconds();
	if (AI && Now >= RoamUntil)
	{
		RoamUntil = Now + 4.f;
		FNavLocation Dest;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (Nav && Nav->GetRandomReachablePointInRadius(GetActorLocation(), 3000.f, Dest))
		{
			Movement->MaxSpeed = BaseSpeed * 0.5f;
			AI->MoveToLocation(Dest.Location, 100.f);
		}
	}
}

void ASeekerMine::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body, bool bSweep, const FHitResult& Hit)
{
	if (HasAuthority() && !IsFriendly(Other))
	{
		StartDetonating();
	}
}

void ASeekerMine::StartDetonating()
{
	// state Detonating: a beep, 0.12 s, boom
	if (bDetonating || bDetonated)
	{
		return;
	}
	bDetonating = true;
	UGameplayStatics::PlaySoundAtLocation(this, SeekerLocal::Sound(SeekerLocal::ActivatingSound), GetActorLocation());
	GetWorldTimerManager().SetTimer(ThinkTimer, this, &ASeekerMine::BlowUp, 0.12f, false);
}

float ASeekerMine::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (HasAuthority() && !bDetonating && DamageAmount > 0.f)
	{
		Health -= FMath::CeilToInt(DamageAmount);
		if (Health <= 0)
		{
			BlowUp();
		}
	}
	return DamageAmount;
}

void ASeekerMine::BlowUp()
{
	if (bDetonated)
	{
		return;
	}
	bDetonated = true;
	// GetDamageInstigator: the placer gets the kills while still on the throwing team
	const ADFCharacter* Placer = Cast<ADFCharacter>(GetInstigator());
	AController* Credit = Placer && (PlacedTeam == DF::NoTeam || Placer->GetTeam() == PlacedTeam) ? Placer->GetController() : nullptr;
	const FVector At = GetActorLocation() + FVector(0.f, 0.f, 36.f);
	TArray<AActor*> Ignore{ this };
	UGameplayStatics::ApplyRadialDamageWithFalloff(this, Damage, 0.f, At, 0.f, DamageRadius, 1.f,
		UDFDamage_SeekerMine::StaticClass(), Ignore, this, Credit, ECC_Visibility);
	ADFEffectActor::Spawn(GetWorld(), TEXT("Explosion"), At, 1.4f);   // AddExplosionServer(Size 3.5)
	Destroy();
}

// --- thrown -----------------------------------------------------------------------------------

ASeekerMineThrown::ASeekerMineThrown()
{
	Movement->InitialSpeed = 2700.f;   // MF speed 1200 uu/s (the throw sets it)
	Movement->MaxSpeed = 6750.f;       // MF MaxSpeed 3000
	Movement->ProjectileGravityScale = 1.f;
	bExplodeOnImpact = true;
	FuseTime = 0.5f;                   // SetTimer(0.5) -> DeployMine
	Damage = 0.f;
}

void ASeekerMineThrown::Explode(const FVector& Location, const FVector& Normal)
{
	if (bExploded)
	{
		return;
	}
	bExploded = true;
	FActorSpawnParameters SP;
	SP.Instigator = GetInstigator();
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	if (ASeekerMine* Mine = GetWorld()->SpawnActor<ASeekerMine>(Location + Normal * 40.f, FRotator(0.f, GetActorRotation().Yaw, 0.f), SP))
	{
		Mine->InitPlacer(Cast<ADFCharacter>(GetInstigator()));
	}
	Destroy();
}

// --- the weapon -------------------------------------------------------------------------------

ASeekerMines::ASeekerMines()
{
	DisplayName = LOCTEXT("SeekerMines", "Seeker Mine");
	MaxClipAmmo = 1;
	AltProjectileClass = ASeekerMineThrown::StaticClass();
	bAddToLoadout = true;
	// Icon converted from the mod's own art (SourceArt/Mods/Icons/SeekerMineIcon.bmp)
	HUDIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/MF/Mods/Icons/SeekerMineIcon.SeekerMineIcon")));
	HUDIconRect = FIntRect(0, 0, 64, 64);
}

void ASeekerMines::StartFire(bool bAlt)
{
	Super::StartFire(true);   // SeekerMines.Fire -> AltFire: both buttons wind up a throw
}

void ASeekerMines::StopFire(bool bAlt)
{
	Super::StopFire(true);
}

bool ASeekerMines::CanPlace() const
{
	const ADFCharacter* Me = GetOwnerCharacter();
	int32 Active = 0;
	for (TActorIterator<ASeekerMine> It(GetWorld()); It; ++It)
	{
		Active += It->GetInstigator() == Me && !It->IsActorBeingDestroyed();
	}
	if (Active < MaxActiveMines)
	{
		return true;
	}
	if (APlayerController* PC = Me ? Cast<APlayerController>(Me->GetController()) : nullptr)
	{
		PC->ClientMessage(FString::Printf(TEXT("Max active Seeker Mines reached: %d"), MaxActiveMines));
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
