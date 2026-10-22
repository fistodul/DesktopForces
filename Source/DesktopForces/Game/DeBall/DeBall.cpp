#include "Game/DeBall/DeBall.h"
#include "DesktopForces.h"
#include "Core/DFContent.h"
#include "Core/DFProps.h"
#include "Core/DFGameSettings.h"
#include "Net/UnrealNetwork.h"
#include "Game/DFGameState.h"
#include "World/DFMapActors.h"
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

ADeBall::ADeBall()
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

void ADeBall::BeginPlay()
{
	Super::BeginPlay();
	// Rage.BallMesh is ~1.06 m across: scaled to the 2 m collision sphere
	DF::ApplyPropMesh(Mesh, TEXT("Rage/BallMesh"), 1.9f);
	UPhysicalMaterial* Rubber = NewObject<UPhysicalMaterial>(this);
	Rubber->Restitution = 0.65f;                      // SMT_RUBBER
	Rubber->Friction = 0.6f;
	Rubber->RestitutionCombineMode = EFrictionCombineMode::Max;
	Sphere->SetPhysMaterialOverride(Rubber);
}

float ADeBall::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// TestMoBallProp.TakeDamage: shots push it (shotgun pellets 40 % of the time), never hurt it
	if (!HasAuthority() || Damage <= 0.f)
	{
		return 0.f;
	}
	if (DamageEvent.DamageTypeClass && DamageEvent.DamageTypeClass->IsChildOf<UDFDamage_Shotgun>() && FMath::FRand() >= 0.4f)
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

void ADeBall::FellOutOfWorld(const UDamageType& DmgType)
{
	Destroy();
}

void ADeBall::Expire(float Seconds)
{
	if (!bExpiring)
	{
		bExpiring = true;
		SetLifeSpan(Seconds);
	}
}

void ADeBall::Destroyed()
{
	if (HasAuthority())
	{
		if (ADeBallGame* GM = GetWorld()->GetAuthGameMode<ADeBallGame>())
		{
			GM->BallGone(this);
		}
	}
	Super::Destroyed();
}

// --- goal ------------------------------------------------------------------------------------

ADeBallGoal::ADeBallGoal()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	Ring = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ring"));
	Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ring->SetCastShadow(false);
	Ring->SetCanEverAffectNavigation(false);
	RootComponent = Ring;
}

void ADeBallGoal::BeginPlay()
{
	Super::BeginPlay();
	Ring->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Game/MF/Meshes/SM_MF_Ring.SM_MF_Ring"), nullptr, LOAD_NoWarn | LOAD_Quiet));
	OnRep_Team();
}

void ADeBallGoal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADeBallGoal, Team);
}

bool ADeBallGoal::Contains(const FVector& P) const
{
	// MoBallGoal.TestOverlap: within CollisionRadius 192 + 128 uu, CollisionHeight up or down
	// (+ the ball's size); measured from the trailer park, not the ring's base
	const FVector D = P - Centre;
	return FMath::Abs(D.Z) < HalfHeight && FVector2D(D).Size() < Radius;
}

void ADeBallGoal::Init(uint8 InTeam)
{
	Team = InTeam;
	Centre = GetActorLocation();
	// the ring stands on the ground under the trailer park, as tall as the scoring cylinder
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFGoal), false, this);
	float Base = Centre.Z - HalfHeight;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Centre, Centre - FVector(0.f, 0.f, HalfHeight), ECC_WorldStatic, Params))
	{
		Base = Hit.ImpactPoint.Z;
	}
	SetActorLocation(FVector(Centre.X, Centre.Y, Base));
	SetActorScale3D(FVector(Radius / 50.f, Radius / 50.f, (Centre.Z + HalfHeight - Base) / 100.f));
	OnRep_Team();
}

void ADeBallGoal::OnRep_Team()
{
	const FLinearColor C = GetDefault<UDFGameSettings>()->GetTeamColor(Team);
	Ring->SetCustomPrimitiveDataVector4(0, FVector4(C.R, C.G, C.B, 1.f));
}

// --- mode ------------------------------------------------------------------------------------

ADeBallGame::ADeBallGame()
{
	GameName = TEXT("DeBall");
	MFGameClass = TEXT("MoBallGame");
	NumTeams = 2;
	GoalTeamScore = 4;          // MoBallGame.GoalTeamScore 4
	TimeLimit = 15;
	bScoreTeamKills = false;
	bVehicles = true;           // bAlwaysUseVehicles
}

void ADeBallGame::InitGameState()
{
	Super::InitGameState();
	if (ADFGameState* GS = GetGameState<ADFGameState>(); GS && bRestrictWeapons)
	{
		GS->AllowedItemGroups = AllowedItemGroups;
	}
}

void ADeBallGame::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();
	// PostBeginPlay: goals at the trailer parks, the ball at the trailer generator
	for (ADFLegacyActor* L : ADFLegacyActor::FindAll(GetWorld(), TEXT("TrailerPark")))
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ADeBallGoal* G = GetWorld()->SpawnActor<ADeBallGoal>(L->GetActorLocation(), FRotator::ZeroRotator, SP))
		{
			G->Init(L->Team == 1 ? uint8(1) : uint8(0));
			Goals.Add(G);
		}
	}
	const TArray<ADFLegacyActor*> Gen = ADFLegacyActor::FindAll(GetWorld(), TEXT("TrailerGenerator"));
	BallStart = Gen.Num() ? Gen[0]->GetActorTransform() : FTransform::Identity;
	BallStart.AddToTranslation(FVector(0.f, 0.f, 225.f));   // Location + (0, 0, 100 uu)
	UE_LOG(LogDF, Log, TEXT("DeBall: %d goals (%s), ball at %s"), Goals.Num(),
		*FString::JoinBy(Goals, TEXT(", "), [](const ADeBallGoal* G) { return G->GetActorLocation().ToCompactString(); }), *BallStart.GetLocation().ToCompactString());
	SpawnBall();
	GetWorldTimerManager().SetTimer(GoalTimer, this, &ADeBallGame::CheckGoals, 0.1f, true);
}

void ADeBallGame::SpawnBall()
{
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Ball = GetWorld()->SpawnActor<ADeBall>(ADeBall::StaticClass(), BallStart, SP);
	Event(TEXT("New ball!"), DF::NoTeam);   // MoBallMessage 2
}

void ADeBallGame::BallGone(ADeBall* Gone)
{
	if (Gone == Ball && IsMatchInProgress())
	{
		Ball = nullptr;
		GetWorldTimerManager().SetTimer(SpawnTimer, this, &ADeBallGame::SpawnBall, 2.f, false);
	}
}

void ADeBallGame::CheckGoals()
{
	ADFGameState* GS = GetGameState<ADFGameState>();
	if (!Ball || Ball->IsExpiring() || !GS || !IsMatchInProgress())
	{
		return;
	}
	const FVector B = Ball->GetActorLocation();
	for (const ADeBallGoal* G : Goals)
	{
		if (G && G->Contains(B))
		{
			const uint8 Scorer = 1 - G->Team;                // into your own goal: the others score
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

void ADeBallGame::OnTimeLimit()
{
	const ADFGameState* GS = GetGameState<ADFGameState>();
	if (!GS || GS->TeamScores.Num() < 2)
	{
		return;
	}
	if (GS->TeamScores[0] == GS->TeamScores[1])
	{
		if (!bOverTime)
		{
			bOverTime = true;                                    // CheckMoBallScores: next goal wins
			Event(TEXT("Sudden death: next goal wins!"), DF::NoTeam);
		}
		return;
	}
	FinishMatch(FString::Printf(TEXT("%s Team Wins!"), *GS->GetTeamName(GS->TeamScores[0] > GS->TeamScores[1] ? 0 : 1).ToString()));
}

AActor* ADeBallGame::GetBotDriveTarget(AController* Bot) const
{
	return Ball;
}

void ADeBallGame::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const
{
	if (Ball)
	{
		OutGoals.Add(Ball);   // FindSpecialAttractionForX: everyone goes for the ball
	}
}
