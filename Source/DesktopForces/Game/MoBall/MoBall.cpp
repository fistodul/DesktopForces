#include "Game/MoBall/MoBall.h"
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
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

// --- ball ------------------------------------------------------------------------------------

AMoBall::AMoBall() {
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicatingMovement(true);
	bAlwaysRelevant = true;

	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->InitSphereRadius(100.f); // ball.COL: sphere, radius 1 m
	Sphere->SetCollisionProfileName(TEXT("PhysicsActor"));
	Sphere->SetSimulatePhysics(true);
	Sphere->BodyInstance.SetMassOverride(30.f, true); // handling Mass 10 (light against 800 - 3000 kg vehicles)
	Sphere->SetLinearDamping(0.15f);
	Sphere->SetAngularDamping(0.4f);
	Sphere->SetNotifyRigidBodyCollision(true);
	// MF's ball is in a soldier's way and takes Physical.Bump's nudge, no more. A capsule walking
	// into the physics sphere would knock it off at 1.65 x walking speed (rubber on a body that
	// can't give way), so soldiers meet a query-only shell instead, like a vehicle's
	Sphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	RootComponent = Sphere;
	PawnBlocker = CreateDefaultSubobject<USphereComponent>(TEXT("PawnBlocker"));
	PawnBlocker->SetupAttachment(Sphere);
	PawnBlocker->InitSphereRadius(100.f);
	PawnBlocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PawnBlocker->SetCollisionResponseToAllChannels(ECR_Ignore);
	PawnBlocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	PawnBlocker->BodyInstance.bAutoWeld = false;
	PawnBlocker->SetCanEverAffectNavigation(false);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Sphere);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Ball(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Ball.Succeeded()) {
		Mesh->SetStaticMesh(Ball.Object);
		Mesh->SetRelativeScale3D(FVector(2.f));
	}
}

void AMoBall::BeginPlay() {
	Super::BeginPlay();
	// Rage.BallMesh is ~1.06 m across: scaled to the 2 m collision sphere
	DF::ApplyPropMesh(Mesh, TEXT("Rage/BallMesh"), 1.9f);
	UPhysicalMaterial* Rubber = NewObject<UPhysicalMaterial>(this);
	Rubber->Restitution = 0.65f; // SMT_RUBBER
	Rubber->Friction = 0.6f;
	Rubber->RestitutionCombineMode = EFrictionCombineMode::Max;
	Sphere->SetPhysMaterialOverride(Rubber);
}

float AMoBall::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) {
	// TestMoBallProp.TakeDamage: shots push it (shotgun pellets 40 % of the time), never hurt it
	if (!HasAuthority() || Damage <= 0.f)
		return 0.f;
	if (DamageEvent.DamageTypeClass && DamageEvent.DamageTypeClass->IsChildOf<UDFDamage_Shotgun>() && FMath::FRand() >= 0.4f)
		return 0.f;
	FVector Dir = FVector::UpVector;
	FVector At = GetActorLocation();
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID)) {
		const FPointDamageEvent& P = static_cast<const FPointDamageEvent&>(DamageEvent);
		Dir = P.ShotDirection;
		At = P.HitInfo.ImpactPoint;
	} else if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID)) {
		const FRadialDamageEvent& R = static_cast<const FRadialDamageEvent&>(DamageEvent);
		Dir = (GetActorLocation() - R.Origin).GetSafeNormal();
	}
	// ApplyForce(Normal(Momentum) * Sqrt(Min(|Momentum|, 100000) * 0.01)): grows with the square root
	Sphere->AddImpulseAtLocation(Dir * FMath::Sqrt(FMath::Min(Damage, 1000.f)) * 900.f, At);
	return 0.f;
}

void AMoBall::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) {
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	// Physical.Bump: ApplyForce(Other.Location, Normal(RealLocation - Other.Location) * 0.01), where
	// a shot applies up to 31.6: leaning on the ball creeps it along, nothing like a kick. MF did
	// that once a tick; by time here (60 a second), whatever the frame rate or the moves per frame
	if (!HasAuthority() || MyComp != PawnBlocker || !Cast<APawn>(Other))
		return;
	const double Now = GetWorld()->GetTimeSeconds();
	const float Dt = FMath::Min(float(Now - LastBumpTime), 1.f / 60.f);
	LastBumpTime = Now;
	Sphere->AddImpulse((GetActorLocation() - Other->GetActorLocation()).GetSafeNormal2D() * 0.01f * 900.f * 60.f * Dt);
}

void AMoBall::FellOutOfWorld(const UDamageType& DmgType) {
	Destroy();
}

void AMoBall::Expire(float Seconds) {
	if (!bExpiring) {
		bExpiring = true;
		SetLifeSpan(Seconds);
	}
}

void AMoBall::Destroyed() {
	if (HasAuthority()) {
		if (AMoBallGame* GM = GetWorld()->GetAuthGameMode<AMoBallGame>())
			GM->BallGone(this);
	}
	Super::Destroyed();
}

// --- goal ------------------------------------------------------------------------------------

AMoBallGoal::AMoBallGoal() {
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	Ring = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ring"));
	Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ring->SetCastShadow(false);
	Ring->SetCanEverAffectNavigation(false);
	RootComponent = Ring;
}

void AMoBallGoal::BeginPlay() {
	Super::BeginPlay();
	Ring->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Game/MF/Meshes/SM_MF_Ring.SM_MF_Ring"), nullptr, LOAD_NoWarn | LOAD_Quiet));
	OnRep_Team();
}

void AMoBallGoal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMoBallGoal, Team);
}

bool AMoBallGoal::Contains(const FVector& P) const {
	// MoBallGoal.TestOverlap: within CollisionRadius 192 + 128 uu, CollisionHeight up or down
	// (+ the ball's size); measured from the trailer park, not the ring's base
	const FVector D = P - Centre;
	return FMath::Abs(D.Z) < HalfHeight && FVector2D(D).Size() < Radius;
}

void AMoBallGoal::Init(uint8 InTeam) {
	Team = InTeam;
	Centre = GetActorLocation();
	// the ring stands on the ground under the trailer park, as tall as the scoring cylinder
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFGoal), false, this);
	float Base = Centre.Z - HalfHeight;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Centre, Centre - FVector(0.f, 0.f, HalfHeight), ECC_WorldStatic, Params))
		Base = Hit.ImpactPoint.Z;
	SetActorLocation(FVector(Centre.X, Centre.Y, Base));
	SetActorScale3D(FVector(Radius / 50.f, Radius / 50.f, (Centre.Z + HalfHeight - Base) / 100.f));
	OnRep_Team();
}

void AMoBallGoal::OnRep_Team() {
	const FLinearColor C = GetDefault<UDFGameSettings>()->GetTeamColor(Team);
	Ring->SetCustomPrimitiveDataVector4(0, FVector4(C.R, C.G, C.B, 1.f));
}

// --- mode ------------------------------------------------------------------------------------

AMoBallGame::AMoBallGame() {
	GameName = TEXT("MoBall");
	MFGameClass = TEXT("MoBallGame");
	NumTeams = 2;
	ScoreLimit = 4; // MoBallGame: goals to win
	TimeLimit = 15;
	bScoreTeamKills = false;
	bAlwaysUseVehicles = true;
}

void AMoBallGame::InitGameState() {
	Super::InitGameState();
	if (ADFGameState* GS = GetGameState<ADFGameState>(); GS && bRestrictWeapons)
		GS->AllowedItemGroups = AllowedItemGroups;
}

void AMoBallGame::HandleMatchHasStarted() {
	Super::HandleMatchHasStarted();
	// PostBeginPlay: on MF's maps goals at the trailer parks, the ball at the trailer generator;
	// MoBall's own maps (MB-*) have goal zones and a PropGenerator for the ball
	for (ADFLegacyActor* L : ADFLegacyActor::FindAll(GetWorld(), TEXT("TrailerPark"))) {
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AMoBallGoal* G = GetWorld()->SpawnActor<AMoBallGoal>(L->GetActorLocation(), FRotator::ZeroRotator, SP)) {
			G->Init(L->Team == 1 ? uint8(1) : uint8(0));
			Goals.Add(G);
		}
	}
	for (TActorIterator<ADFZoneBox> It(GetWorld()); It; ++It)
		if (It->ActorHasTag(TEXT("MoBallGoal0")) || It->ActorHasTag(TEXT("MoBallGoal1")) || It->ActorHasTag(TEXT("MoBallDestruct")))
			BallZones.Add(*It);
	TArray<ADFLegacyActor*> Gen = ADFLegacyActor::FindAll(GetWorld(), TEXT("PropGenerator"));
	if (!Gen.Num())
		Gen = ADFLegacyActor::FindAll(GetWorld(), TEXT("TrailerGenerator"));
	BallStart = Gen.Num() ? Gen[0]->GetActorTransform() : FTransform::Identity;
	BallStart.AddToTranslation(FVector(0.f, 0.f, 225.f)); // Location + (0, 0, 100 uu)
	if (!Gen.Num() || Goals.Num() + BallZones.Num() == 0)
		UE_LOG(LogDF, Warning, TEXT("MoBall: this map has %s"), Gen.Num() ? TEXT("no goals") : TEXT("no place for the ball"));
	UE_LOG(LogDF, Log, TEXT("MoBall: %d goals (%s), %d ball zones, ball at %s"), Goals.Num(),
		*FString::JoinBy(Goals, TEXT(", "), [](const AMoBallGoal* G) { return G->GetActorLocation().ToCompactString(); }), BallZones.Num(), *BallStart.GetLocation().ToCompactString());
	SpawnBall();
	GetWorldTimerManager().SetTimer(GoalTimer, this, &AMoBallGame::CheckGoals, 0.1f, true);
}

void AMoBallGame::SpawnBall() {
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Ball = GetWorld()->SpawnActor<AMoBall>(AMoBall::StaticClass(), BallStart, SP);
	Event(TEXT("New ball!"), DF::NoTeam); // MoBallMessage 2
}

void AMoBallGame::BallGone(AMoBall* Gone) {
	if (Gone == Ball && IsMatchInProgress()) {
		Ball = nullptr;
		GetWorldTimerManager().SetTimer(SpawnTimer, this, &AMoBallGame::SpawnBall, 2.f, false);
	}
}

void AMoBallGame::CheckGoals() {
	ADFGameState* GS = GetGameState<ADFGameState>();
	if (!Ball || Ball->IsExpiring() || !GS || !IsMatchInProgress())
		return;
	const FVector B = Ball->GetActorLocation();
	uint8 GoalOf = DF::NoTeam;
	for (const AMoBallGoal* G : Goals)
		if (G && G->Contains(B))
			GoalOf = G->Team;
	for (const ADFZoneBox* Z : BallZones) {
		if (!Z || !Z->IsInside(B))
			continue;
		if (Z->ActorHasTag(TEXT("MoBallDestruct"))) {
			Ball->Expire(2.f); // MoBallDestructZone: a pit, the edge of the map; no score
			return;
		}
		GoalOf = Z->ActorHasTag(TEXT("MoBallGoal1")) ? 1 : 0;
	}
	if (GoalOf == DF::NoTeam)
		return;
	const uint8 Scorer = 1 - GoalOf; // into your own goal: the others score
	GS->TeamScores[Scorer] += 1;
	Event(FString::Printf(TEXT("%s Team Score!"), *TeamName(Scorer)), Scorer);
	Ball->Expire(5.f); // LifeSpan = 5
	if (bOverTime || (ScoreLimit > 0 && GS->TeamScores[Scorer] >= ScoreLimit))
		TeamWins(Scorer);
}

void AMoBallGame::OnTimeLimit() {
	const ADFGameState* GS = GetGameState<ADFGameState>();
	if (!GS || GS->TeamScores.Num() < 2)
		return;
	if (GS->TeamScores[0] == GS->TeamScores[1]) {
		if (!bOverTime) {
			bOverTime = true; // CheckMoBallScores: next goal wins
			Event(TEXT("Sudden death: next goal wins!"), DF::NoTeam);
		}
		return;
	}
	TeamWins(GS->TeamScores[0] > GS->TeamScores[1] ? 0 : 1);
}

AActor* AMoBallGame::GetBotDriveTarget(AController* Bot) const {
	return Ball;
}

void AMoBallGame::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const {
	if (Ball)
		OutGoals.Add(Ball); // FindSpecialAttractionForX: everyone goes for the ball
}
