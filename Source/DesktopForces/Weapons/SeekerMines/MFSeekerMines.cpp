#include "Weapons/SeekerMines/MFSeekerMines.h"
#include "World/MFEffectActor.h"
#include "Core/MFGameSettings.h"
#include "Player/MFCharacter.h"
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

#define LOCTEXT_NAMESPACE "MFModSeekerMines"

namespace MFSeekerLocal
{
	USoundBase* Sound(const TCHAR* Path)
	{
		return LoadObject<USoundBase>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	const TCHAR* ActivatingSound = TEXT("/MobileForcesClassic/Sounds/WeaponSFX_TripBombs/Activating.Activating");
}

UMFDamage_SeekerMine::UMFDamage_SeekerMine()
{
	bExplosive = true;
	VehicleDamageScale = 1.5f;
	DeathVerb = LOCTEXT("HuntedDown", "hunted down");
}

// --- the drone --------------------------------------------------------------------------

AMFSeekerMine::AMFSeekerMine()
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

void AMFSeekerMine::InitPlacer(AMFCharacter* Placer)
{
	SetInstigator(Placer);
	PlacedTeam = Placer ? Placer->GetTeam() : MF::NoTeam;
}

void AMFSeekerMine::BeginPlay()
{
	Super::BeginPlay();
	BaseSpeed = Movement->MaxSpeed;
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (UMaterialInterface* M = UMFGameSettings::Get().GetTeamTintMaterial())
		{
			Mesh->SetMaterial(0, M);
		}
	}
	Collision->OnComponentBeginOverlap.AddDynamic(this, &AMFSeekerMine::OnOverlap);
	if (HasAuthority())
	{
		UGameplayStatics::PlaySoundAtLocation(this, MFSeekerLocal::Sound(MFSeekerLocal::ActivatingSound), GetActorLocation());
		// state Deploying: 0.35 s, then hunt
		GetWorldTimerManager().SetTimer(ThinkTimer, this, &AMFSeekerMine::Think, 0.2f, true, 0.35f);
	}
}

bool AMFSeekerMine::IsFriendly(const AActor* Other) const
{
	const AMFCharacter* C = Cast<AMFCharacter>(Other);
	if (!C || !C->IsAlive())
	{
		return true;   // only living players are worth chasing
	}
	return C == GetInstigator() || (PlacedTeam != MF::NoTeam && C->GetTeam() == PlacedTeam);
}

AMFCharacter* AMFSeekerMine::FindBestTarget() const
{
	AMFCharacter* Best = nullptr;
	float BestDist = SeekRadius;
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
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

void AMFSeekerMine::MoveTo(AActor* Goal, float Acceptance, float SpeedScale)
{
	Movement->MaxSpeed = BaseSpeed * SpeedScale;
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->MoveToActor(Goal, Acceptance, true, true, true, nullptr, true);
	}
}

void AMFSeekerMine::Think()
{
	if (bDetonating)
	{
		return;
	}
	AAIController* AI = Cast<AAIController>(GetController());
	// state Hunting
	if (AMFCharacter* Target = FindBestTarget())
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
	const AMFCharacter* Owner = Cast<AMFCharacter>(GetInstigator());
	const float OwnerDist = Owner ? FVector::Dist(Owner->GetActorLocation(), GetActorLocation()) : 1e9f;
	if (Owner && Owner->IsAlive() && OwnerDist < 5625.f)
	{
		if (OwnerDist > 1012.f)
		{
			MoveTo(const_cast<AMFCharacter*>(Owner), 600.f, 0.85f);
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

void AMFSeekerMine::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body, bool bSweep, const FHitResult& Hit)
{
	if (HasAuthority() && !IsFriendly(Other))
	{
		StartDetonating();
	}
}

void AMFSeekerMine::StartDetonating()
{
	// state Detonating: a beep, 0.12 s, boom
	if (bDetonating || bDetonated)
	{
		return;
	}
	bDetonating = true;
	UGameplayStatics::PlaySoundAtLocation(this, MFSeekerLocal::Sound(MFSeekerLocal::ActivatingSound), GetActorLocation());
	GetWorldTimerManager().SetTimer(ThinkTimer, this, &AMFSeekerMine::BlowUp, 0.12f, false);
}

float AMFSeekerMine::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
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

void AMFSeekerMine::BlowUp()
{
	if (bDetonated)
	{
		return;
	}
	bDetonated = true;
	// GetDamageInstigator: the placer gets the kills while still on the throwing team
	const AMFCharacter* Placer = Cast<AMFCharacter>(GetInstigator());
	AController* Credit = Placer && (PlacedTeam == MF::NoTeam || Placer->GetTeam() == PlacedTeam) ? Placer->GetController() : nullptr;
	const FVector At = GetActorLocation() + FVector(0.f, 0.f, 36.f);
	TArray<AActor*> Ignore{ this };
	UGameplayStatics::ApplyRadialDamageWithFalloff(this, Damage, 0.f, At, 0.f, DamageRadius, 1.f,
		UMFDamage_SeekerMine::StaticClass(), Ignore, this, Credit, ECC_Visibility);
	AMFEffectActor::Spawn(GetWorld(), TEXT("Explosion"), At, 1.4f);   // AddExplosionServer(Size 3.5)
	Destroy();
}

// --- thrown -----------------------------------------------------------------------------------

AMFSeekerMineThrown::AMFSeekerMineThrown()
{
	Movement->InitialSpeed = 2700.f;   // MF speed 1200 uu/s (the throw sets it)
	Movement->MaxSpeed = 6750.f;       // MF MaxSpeed 3000
	Movement->ProjectileGravityScale = 1.f;
	bExplodeOnImpact = true;
	FuseTime = 0.5f;                   // SetTimer(0.5) -> DeployMine
	Damage = 0.f;
}

void AMFSeekerMineThrown::Explode(const FVector& Location, const FVector& Normal)
{
	if (bExploded)
	{
		return;
	}
	bExploded = true;
	FActorSpawnParameters SP;
	SP.Instigator = GetInstigator();
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	if (AMFSeekerMine* Mine = GetWorld()->SpawnActor<AMFSeekerMine>(Location + Normal * 40.f, FRotator(0.f, GetActorRotation().Yaw, 0.f), SP))
	{
		Mine->InitPlacer(Cast<AMFCharacter>(GetInstigator()));
	}
	Destroy();
}

// --- the weapon -------------------------------------------------------------------------------

AMFWeapon_SeekerMines::AMFWeapon_SeekerMines()
{
	DisplayName = LOCTEXT("SeekerMines", "Seeker Mine");
	MaxClipAmmo = 1;
	AltProjectileClass = AMFSeekerMineThrown::StaticClass();
	bAddToLoadout = true;
	// Icon converted from the mod's own art (SourceArt/Mods/Icons/SeekerMineIcon.bmp)
	HUDIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/MF/Mods/Icons/SeekerMineIcon.SeekerMineIcon")));
	HUDIconRect = FIntRect(0, 0, 64, 64);
}

void AMFWeapon_SeekerMines::StartFire(bool bAlt)
{
	Super::StartFire(true);   // SeekerMines.Fire -> AltFire: both buttons wind up a throw
}

void AMFWeapon_SeekerMines::StopFire(bool bAlt)
{
	Super::StopFire(true);
}

bool AMFWeapon_SeekerMines::CanPlace() const
{
	const AMFCharacter* Me = GetOwnerCharacter();
	int32 Active = 0;
	for (TActorIterator<AMFSeekerMine> It(GetWorld()); It; ++It)
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
