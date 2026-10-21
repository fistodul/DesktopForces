#include "Game/MFHoldout.h"
#include "Core/MFProps.h"
#include "DesktopForces.h"
#include "Core/MFGameSettings.h"
#include "Game/MFGameState.h"
#include "Game/MFMessages.h"
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
	if (AMFGameMode_Holdout* GM = GetWorld()->GetAuthGameMode<AMFGameMode_Holdout>())
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

AMFGameMode_Holdout::AMFGameMode_Holdout()
{
	GameName = TEXT("Holdout");
	InstructionSound = TEXT("Hold_the_Point");
	NumTeams = 2;
	GoalTeamScore = 6;     // minutes of hold time (RageDomination.GoalTeamScore)
	TimeLimit = 20;
	FragLimit = 0;
	bScoreTeamKills = false;
}

AMFHoldoutPoint* AMFGameMode_Holdout::SpawnPointAt(const FTransform& Where, const FString& Name)
{
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMFHoldoutPoint* P = GetWorld()->SpawnActor<AMFHoldoutPoint>(AMFHoldoutPoint::StaticClass(), Where, SP);
	if (P)
	{
		P->TeamTimes.Init(0, NumTeams);
		P->PointName = Name;
		Points.Add(P);
		UE_LOG(LogMF, Log, TEXT("Holdout: %s at %s"), *Name, *Where.GetLocation().ToCompactString());
	}
	return P;
}

void AMFGameMode_Holdout::SpawnPoints()
{
	// Points placed directly in the map win; otherwise use MF's Holdout timer
	for (TActorIterator<AMFHoldoutPoint> It(GetWorld()); It; ++It)
	{
		It->TeamTimes.Init(0, NumTeams);
		Points.Add(*It);
	}
	if (Points.Num() == 0)
	{
		// MF bSingleTarget: Domination uses the first timer
		const TArray<AMFLegacyActor*> Timers = AMFLegacyActor::FindAll(GetWorld(), TEXT("RageDominationTimer"));
		if (Timers.Num() > 0)
		{
			SpawnPointAt(Timers[0]->GetActorTransform(), TEXT("Holdout"));
		}
	}
}

void AMFGameMode_Holdout::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();
	SpawnPoints();
	GetWorldTimerManager().SetTimer(PointTimer, this, &AMFGameMode_Holdout::UpdatePoints, 1.f, true);
}

void AMFGameMode_Holdout::HandleMatchHasEnded()
{
	GetWorldTimerManager().ClearTimer(PointTimer);
	Super::HandleMatchHasEnded();
}

void AMFGameMode_Holdout::PointCaptured(AMFHoldoutPoint* Point, AMFCharacter* By)
{
	UE_LOG(LogMF, Log, TEXT("Holdout: %s taken by team %d (%s)"), *Point->PointName, Point->OwnerTeam, *GetNameSafe(By));
	// RageDomination.ScoreTimer: announce the capture
	if (AMFPlayerState* PS = By->GetPlayerState<AMFPlayerState>())
	{
		BroadcastLocalized(this, UMFAnnouncement::StaticClass(), UMFAnnouncement::PointCaptured, PS, nullptr, Point);
	}
}

void AMFGameMode_Holdout::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const
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

void AMFGameMode_Holdout::UpdatePoints()
{
	AMFGameState* GS = GetGameState<AMFGameState>();
	if (!GS || !IsMatchInProgress())
	{
		return;
	}
	// RageDominationTimer.UpdateTimer: owning team +1 s and +1 team score each second
	for (AMFHoldoutPoint* P : Points)
	{
		if (P && P->TeamTimes.IsValidIndex(P->OwnerTeam))
		{
			P->TeamTimes[P->OwnerTeam] += 1;
			GS->TeamScores[P->OwnerTeam] += 1;
			if (GoalTeamScore > 0 && P->TeamTimes[P->OwnerTeam] >= GoalTeamScore * 60)
			{
				OnTimeLimit();   // RageDomination.TimerLimit: most time wins
				return;
			}
		}
	}
}

void AMFGameMode_Holdout::OnTimeLimit()
{
	Super::OnTimeLimit();   // team with the highest score (= hold time) wins
}
