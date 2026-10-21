#include "Game/KOH/MFGameMode_KOH.h"
#include "Core/MFProps.h"
#include "DesktopForces.h"
#include "Core/MFGameSettings.h"
#include "Game/MFGameState.h"
#include "Game/MFMessages.h"
#include "Game/MFPlayerStart.h"
#include "Game/MFPlayerState.h"
#include "Player/MFCharacter.h"
#include "World/MFMapActors.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

// --- capture point -----------------------------------------------------------------------

AMFHoldoutPoint::AMFHoldoutPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;   // MF: bAlwaysRelevant, shown on everyone's HUD/minimap
	SetNetUpdateFrequency(3.f);   // MF NetUpdateFrequency 3

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(135.f);   // MF CollisionRadius 60
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AMFHoldoutPoint::OnOverlap);
	RootComponent = Trigger;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Trigger);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cyl.Succeeded())
	{
		Mesh->SetStaticMesh(Cyl.Object);
		Mesh->SetRelativeScale3D(FVector(0.6f, 0.6f, 1.2f));   // placeholder until the MF timer mesh is converted
	}
}

void AMFHoldoutPoint::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFHoldoutPoint, OwnerTeam);
	DOREPLIFETIME(AMFHoldoutPoint, TeamTimes);
	DOREPLIFETIME(AMFHoldoutPoint, PointName);
}

void AMFHoldoutPoint::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		// Someone may already stand here when the point spawns (overlaps start before BeginPlay)
		TArray<AActor*> Inside;
		Trigger->GetOverlappingActors(Inside, AMFCharacter::StaticClass());
		for (AActor* A : Inside)
		{
			OnOverlap(Trigger, A, nullptr, 0, false, FHitResult());
		}
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (UMaterialInterface* M = UMFGameSettings::Get().GetTeamTintMaterial())
		{
			Mesh->SetMaterial(0, M);
		}
		UpdateLook();
	}
}

void AMFHoldoutPoint::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body, bool bSweep, const FHitResult& Hit)
{
	AMFCharacter* C = Cast<AMFCharacter>(Other);
	if (!HasAuthority() || !C || !C->IsAlive() || C->GetTeam() == MF::NoTeam || C->GetTeam() == OwnerTeam)
	{
		return;
	}
	SetOwnerTeam(C->GetTeam());
	if (AMFGameMode_KOH* GM = GetWorld()->GetAuthGameMode<AMFGameMode_KOH>())
	{
		GM->PointCaptured(this, C);
	}
}

void AMFHoldoutPoint::SetOwnerTeam(uint8 Team)
{
	OwnerTeam = Team;
	UpdateLook();
}

void AMFHoldoutPoint::OnRep_OwnerTeam()
{
	UpdateLook();
}

void AMFHoldoutPoint::UpdateLook()
{
	// MF RageDominationTimer: neutral / TeamMesh(0) blue / TeamMesh(1) red
	if (MF::ApplyPropMesh(Mesh, OwnerTeam == 0 ? TEXT("RageGame/RageDomTimerBlueMesh")
		: OwnerTeam == 1 ? TEXT("RageGame/RageDomTimerRedMesh") : TEXT("RageGame/RageDomTimerMesh")))
	{
		return;
	}
	const FLinearColor C = OwnerTeam == MF::NoTeam ? FLinearColor(0.7f, 0.7f, 0.7f) : UMFGameSettings::Get().GetTeamColor(OwnerTeam);
	Mesh->SetCustomPrimitiveDataVector4(0, C);
}

// --- game mode -------------------------------------------------------------------------

AMFGameMode_KOH::AMFGameMode_KOH()
{
	GameName = TEXT("King of the Hill");
	MFGameClass = TEXT("KOHGame");
	InstructionSound = TEXT("Hold_the_Point");
	NumTeams = 2;
	GoalTeamScore = 700;   // team points
	FragLimit = 0;
	TimeLimit = 0;
	bScoreTeamKills = false;
}

AMFHoldoutPoint* AMFGameMode_KOH::SpawnPointAt(const FTransform& Where, const FString& Name)
{
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMFHoldoutPoint* P = GetWorld()->SpawnActor<AMFHoldoutPoint>(AMFHoldoutPoint::StaticClass(), Where, SP);
	if (P)
	{
		P->TeamTimes.Init(0, NumTeams);
		P->PointName = Name;
		Points.Add(P);
		UE_LOG(LogMF, Log, TEXT("Capture point: %s at %s"), *Name, *Where.GetLocation().ToCompactString());
	}
	return P;
}

bool AMFGameMode_KOH::PickPoint(TArray<FCandidate>& Pool, TArray<FTransform>& Chosen, const FVector* Ref, int32 PreferredType, float MinDist) const
{
	// KOHGame.PickBestCandidate: nearest to Ref (or random), +preference for the type, spaced out
	int32 Best = INDEX_NONE;
	float BestScore = FLT_MAX;
	for (int32 i = 0; i < Pool.Num(); ++i)
	{
		const FVector Loc = Pool[i].Where.GetLocation();
		if (Chosen.ContainsByPredicate([&](const FTransform& C) { return FVector::Dist(C.GetLocation(), Loc) < MinDist; }))
		{
			continue;
		}
		float Score = Ref ? FVector::Dist(Loc, *Ref) : FMath::FRand() * 2250.f;
		if (Pool[i].Type == PreferredType)
		{
			Score -= 11250.f;   // MF: -5000 uu preference bonus
		}
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = i;
		}
	}
	if (Best == INDEX_NONE)
	{
		return false;
	}
	Chosen.Add(Pool[Best].Where);
	Pool.RemoveAt(Best);
	return true;
}

void AMFGameMode_KOH::SpawnPoints()
{
	// points placed directly in the map win
	for (TActorIterator<AMFHoldoutPoint> It(GetWorld()); It; ++It)
	{
		It->TeamTimes.Init(0, NumTeams);
		Points.Add(*It);
	}
	if (Points.Num() > 0)
	{
		return;
	}
	TArray<FCandidate> Pool;
	auto AddAll = [&](FName Class, int32 Type)
	{
		for (AMFLegacyActor* L : AMFLegacyActor::FindAll(GetWorld(), Class))
		{
			Pool.Add({ L->GetActorTransform(), Type });
		}
	};
	AddAll(TEXT("RageDominationTimer"), 0);
	AddAll(TEXT("RageDetonationLock"), 1);
	AddAll(TEXT("RageDetPossibleKeyPos"), 2);

	// Team bases = average of each team's starts; neutral starts far from them join the pool
	TMap<uint8, FVector> BaseSum;
	TMap<uint8, int32> BaseCount;
	TArray<AMFPlayerStart*> Neutral;
	for (TActorIterator<AMFPlayerStart> It(GetWorld()); It; ++It)
	{
		if (It->IsNeutral())
		{
			Neutral.Add(*It);
		}
		else
		{
			BaseSum.FindOrAdd(It->TeamNumber) += It->GetActorLocation();
			BaseCount.FindOrAdd(It->TeamNumber)++;
		}
	}
	TArray<FVector> Bases;
	for (const auto& Pair : BaseSum)
	{
		Bases.Add(Pair.Value / BaseCount[Pair.Key]);
	}
	for (AMFPlayerStart* S : Neutral)
	{
		const bool bNearBase = Bases.ContainsByPredicate([&](const FVector& B) { return FVector::Dist(B, S->GetActorLocation()) < MinDistance * 2.f; });
		if (!bNearBase)
		{
			Pool.Add({ S->GetActorTransform(), 2 });
		}
	}

	TArray<FTransform> Chosen;
	if (bCentralizedSpawning && Bases.Num() > 0)
	{
		FVector Centre = FVector::ZeroVector;
		for (const FVector& B : Bases)
		{
			Centre += B;
		}
		Centre /= Bases.Num();
		PickPoint(Pool, Chosen, &Centre, 0, MinDistance);
		if (PointsToSpawn >= 3)
		{
			for (const FVector& B : Bases)
			{
				if (Chosen.Num() < PointsToSpawn)
				{
					PickPoint(Pool, Chosen, &B, 1, MinDistance);
				}
			}
		}
	}
	while (Chosen.Num() < PointsToSpawn && PickPoint(Pool, Chosen, nullptr, 2, MinDistance)) {}
	while (Chosen.Num() < PointsToSpawn && PickPoint(Pool, Chosen, nullptr, 2, MinDistance * 0.5f)) {}

	for (int32 i = 0; i < Chosen.Num(); ++i)
	{
		SpawnPointAt(Chosen[i], FString::Printf(TEXT("Point %c"), TCHAR('A' + i % 26)));
	}
}

void AMFGameMode_KOH::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();
	SpawnPoints();
	GetWorldTimerManager().SetTimer(PointTimer, this, &AMFGameMode_KOH::UpdatePoints, 1.f, true);
}

void AMFGameMode_KOH::HandleMatchHasEnded()
{
	GetWorldTimerManager().ClearTimer(PointTimer);
	Super::HandleMatchHasEnded();
}

void AMFGameMode_KOH::PointCaptured(AMFHoldoutPoint* Point, AMFCharacter* By)
{
	UE_LOG(LogMF, Log, TEXT("%s: %s taken by team %d (%s)"), *GameName, *Point->PointName, Point->OwnerTeam, *GetNameSafe(By));
	// KOHGame / RageDomination.ScoreTimer: the bonus, the announcement
	if (AMFPlayerState* PS = By->GetPlayerState<AMFPlayerState>())
	{
		PS->SetScore(PS->GetScore() + CaptureBonus);
		BroadcastLocalized(this, UMFAnnouncement::StaticClass(), UMFAnnouncement::PointCaptured, PS, nullptr, Point);
	}
}

void AMFGameMode_KOH::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const
{
	const AMFPlayerState* PS = Bot ? Bot->GetPlayerState<AMFPlayerState>() : nullptr;
	const APawn* Pawn = Bot ? Bot->GetPawn() : nullptr;
	if (!PS || !Pawn)
	{
		return;
	}
	TArray<TPair<float, AActor*>> Attack;
	TArray<AActor*> Guard;
	for (AMFHoldoutPoint* P : Points)
	{
		if (!P)
		{
			continue;
		}
		if (P->OwnerTeam == PS->GetTeam())
		{
			Guard.Insert(P, FMath::RandHelper(Guard.Num() + 1));
		}
		else
		{
			Attack.Emplace(FVector::Dist(P->GetActorLocation(), Pawn->GetActorLocation()) * FMath::FRandRange(0.6f, 1.4f), P);
		}
	}
	Attack.Sort([](const TPair<float, AActor*>& A, const TPair<float, AActor*>& B) { return A.Key < B.Key; });
	// A third of the time someone stays behind to guard (MF RageDefensePoint behaviour)
	const bool bGuardFirst = Guard.Num() > 0 && FMath::FRand() < 0.33f;
	if (bGuardFirst)
	{
		OutGoals.Append(Guard);
	}
	for (const TPair<float, AActor*>& A : Attack)
	{
		OutGoals.Add(A.Value);
	}
	if (!bGuardFirst)
	{
		OutGoals.Append(Guard);
	}
}

void AMFGameMode_KOH::UpdatePoints()
{
	AMFGameState* GS = GetGameState<AMFGameState>();
	if (!GS || !IsMatchInProgress())
	{
		return;
	}
	// KOHGame.Timer: +1 team score per second per point held
	for (AMFHoldoutPoint* P : Points)
	{
		if (P && P->TeamTimes.IsValidIndex(P->OwnerTeam))
		{
			P->TeamTimes[P->OwnerTeam] += 1;
			GS->TeamScores[P->OwnerTeam] += 1;
		}
	}
	for (int32 T = 0; T < GS->TeamScores.Num(); ++T)
	{
		if (GetGoalScore() > 0 && GS->TeamScores[T] >= GetGoalScore())
		{
			OnTimeLimit();   // highest team score wins
			return;
		}
	}
}

FName AMFGameMode_KOH::GetOrderSound(int32 Order) const
{
	// RageVoicePack.SetOrderMessage: OrderSound 9 / 10 (both "Take the point")
	return Order == 0 || Order == 2 ? FName(TEXT("Take_The_Point")) : Super::GetOrderSound(Order);
}
