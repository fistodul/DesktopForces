#include "Game/MoBall/MFMoBall.h"
#include "DesktopForces.h"
#include "Core/MFContent.h"
#include "Core/MFProps.h"
#include "Game/MFGameState.h"
#include "World/MFMapActors.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

// --- ball ------------------------------------------------------------------------------------

AMFMoBallBall::AMFMoBallBall()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicatingMovement(true);
	bAlwaysRelevant = true;

	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->InitSphereRadius(100.f);                 // ball.COL: sphere, radius 1 m
	Sphere->SetCollisionProfileName(TEXT("PhysicsActor"));
	Sphere->SetSimulatePhysics(true);
	Sphere->BodyInstance.SetMassOverride(30.f, true);    // handling Mass 10 (light against 800 - 3000 kg vehicles)
	Sphere->SetLinearDamping(0.15f);
	Sphere->SetAngularDamping(0.4f);
	Sphere->SetNotifyRigidBodyCollision(true);
	RootComponent = Sphere;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Sphere);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Ball(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Ball.Succeeded())
	{
		Mesh->SetStaticMesh(Ball.Object);
		Mesh->SetRelativeScale3D(FVector(2.f));
	}
}

void AMFMoBallBall::BeginPlay()
{
	Super::BeginPlay();
	// Rage.BallMesh is ~1.06 m across: scaled to the 2 m collision sphere
	MF::ApplyPropMesh(Mesh, TEXT("Rage/BallMesh"), 1.9f);
	UPhysicalMaterial* Rubber = NewObject<UPhysicalMaterial>(this);
	Rubber->Restitution = 0.65f;                      // SMT_RUBBER
	Rubber->Friction = 0.6f;
	Rubber->RestitutionCombineMode = EFrictionCombineMode::Max;
	Sphere->SetPhysMaterialOverride(Rubber);
}

float AMFMoBallBall::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// TestMoBallProp.TakeDamage: shots push it (shotgun pellets 40 % of the time), never hurt it
	if (!HasAuthority() || Damage <= 0.f)
	{
		return 0.f;
	}
	if (DamageEvent.DamageTypeClass && DamageEvent.DamageTypeClass->IsChildOf<UMFDamage_Shotgun>() && FMath::FRand() >= 0.4f)
	{
		return 0.f;
	}
	FVector Dir = FVector::UpVector;
	FVector At = GetActorLocation();
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& P = static_cast<const FPointDamageEvent&>(DamageEvent);
		Dir = P.ShotDirection;
		At = P.HitInfo.ImpactPoint;
	}
	else if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
	{
		const FRadialDamageEvent& R = static_cast<const FRadialDamageEvent&>(DamageEvent);
		Dir = (GetActorLocation() - R.Origin).GetSafeNormal();
	}
	// ApplyForce(Normal(Momentum) * Sqrt(Min(|Momentum|, 100000) * 0.01)): grows with the square root
	Sphere->AddImpulseAtLocation(Dir * FMath::Sqrt(FMath::Min(Damage, 1000.f)) * 900.f, At);
	return 0.f;
}

void AMFMoBallBall::FellOutOfWorld(const UDamageType& DmgType)
{
	Destroy();
}

void AMFMoBallBall::Expire(float Seconds)
{
	if (!bExpiring)
	{
		bExpiring = true;
		SetLifeSpan(Seconds);
	}
}

void AMFMoBallBall::Destroyed()
{
	if (HasAuthority())
	{
		if (AMFGameMode_MoBall* GM = GetWorld()->GetAuthGameMode<AMFGameMode_MoBall>())
		{
			GM->BallGone(this);
		}
	}
	Super::Destroyed();
}

// --- mode ------------------------------------------------------------------------------------

AMFGameMode_MoBall::AMFGameMode_MoBall()
{
	GameName = TEXT("MoBall");
	MFGameClass = TEXT("MoBallGame");
	NumTeams = 2;
	GoalTeamScore = 4;          // MoBallGame.GoalTeamScore 4
	TimeLimit = 15;
	bScoreTeamKills = false;
	bVehicles = true;           // bAlwaysUseVehicles
}

void AMFGameMode_MoBall::InitGameState()
{
	Super::InitGameState();
	if (AMFGameState* GS = GetGameState<AMFGameState>(); GS && bRestrictWeapons)
	{
		GS->AllowedItemGroups = AllowedItemGroups;
	}
}

void AMFGameMode_MoBall::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();
	// PostBeginPlay: goals at the trailer parks, the ball at the trailer generator
	for (AMFLegacyActor* L : AMFLegacyActor::FindAll(GetWorld(), TEXT("TrailerPark")))
	{
		Goals.Add({ L->GetActorLocation(), L->Team == 1 ? uint8(1) : uint8(0) });
	}
	const TArray<AMFLegacyActor*> Gen = AMFLegacyActor::FindAll(GetWorld(), TEXT("TrailerGenerator"));
	BallStart = Gen.Num() ? Gen[0]->GetActorTransform() : FTransform::Identity;
	BallStart.AddToTranslation(FVector(0.f, 0.f, 225.f));   // Location + (0, 0, 100 uu)
	UE_LOG(LogMF, Log, TEXT("MoBall: %d goals, ball at %s"), Goals.Num(), *BallStart.GetLocation().ToCompactString());
	SpawnBall();
	GetWorldTimerManager().SetTimer(GoalTimer, this, &AMFGameMode_MoBall::CheckGoals, 0.1f, true);
}

void AMFGameMode_MoBall::SpawnBall()
{
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Ball = GetWorld()->SpawnActor<AMFMoBallBall>(AMFMoBallBall::StaticClass(), BallStart, SP);
	Event(TEXT("New ball!"), MF::NoTeam);   // MoBallMessage 2
}

void AMFGameMode_MoBall::BallGone(AMFMoBallBall* Gone)
{
	if (Gone == Ball && IsMatchInProgress())
	{
		Ball = nullptr;
		GetWorldTimerManager().SetTimer(SpawnTimer, this, &AMFGameMode_MoBall::SpawnBall, 2.f, false);
	}
}

void AMFGameMode_MoBall::CheckGoals()
{
	AMFGameState* GS = GetGameState<AMFGameState>();
	if (!Ball || Ball->IsExpiring() || !GS || !IsMatchInProgress())
	{
		return;
	}
	// MoBallGoal.TestOverlap: within CollisionRadius 192 + 128 uu of the goal, 64 uu up or down
	const FVector B = Ball->GetActorLocation();
	for (const FGoal& G : Goals)
	{
		const FVector D = B - G.Location;
		if (FMath::Abs(D.Z) < 144.f + 100.f && FVector2D(D).Size() < 720.f)
		{
			const uint8 Scorer = 1 - G.Team;                 // into your own goal: the others score
			GS->TeamScores[Scorer] += 1;
			Event(FString::Printf(TEXT("%s Team Score!"), *GS->GetTeamName(Scorer).ToString()), Scorer);
			Ball->Expire(5.f);                                  // LifeSpan = 5
			if (bOverTime || (GoalTeamScore > 0 && GS->TeamScores[Scorer] >= GoalTeamScore))
			{
				FinishMatch(FString::Printf(TEXT("%s Team Wins!"), *GS->GetTeamName(Scorer).ToString()));
			}
			return;
		}
	}
}

void AMFGameMode_MoBall::OnTimeLimit()
{
	const AMFGameState* GS = GetGameState<AMFGameState>();
	if (!GS || GS->TeamScores.Num() < 2)
	{
		return;
	}
	if (GS->TeamScores[0] == GS->TeamScores[1])
	{
		if (!bOverTime)
		{
			bOverTime = true;                                    // CheckMoBallScores: next goal wins
			Event(TEXT("Sudden death: next goal wins!"), MF::NoTeam);
		}
		return;
	}
	FinishMatch(FString::Printf(TEXT("%s Team Wins!"), *GS->GetTeamName(GS->TeamScores[0] > GS->TeamScores[1] ? 0 : 1).ToString()));
}

AActor* AMFGameMode_MoBall::GetBotDriveTarget(AController* Bot) const
{
	return Ball;
}

void AMFGameMode_MoBall::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const
{
	if (Ball)
	{
		OutGoals.Add(Ball);   // FindSpecialAttractionForX: everyone goes for the ball
	}
}
