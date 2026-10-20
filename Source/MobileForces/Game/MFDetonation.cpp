#include "Game/MFDetonation.h"
#include "MobileForces.h"
#include "AI/MFBotController.h"
#include "Core/MFGameSettings.h"
#include "Core/MFTypes.h"
#include "Game/MFGameState.h"
#include "Game/MFPlayerState.h"
#include "Player/MFCharacter.h"
#include "World/MFMapActors.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	AMFGameMode_Detonation* DetMode(const UObject* Ctx)
	{
		return Ctx->GetWorld() ? Ctx->GetWorld()->GetAuthGameMode<AMFGameMode_Detonation>() : nullptr;
	}
}

// --- lock --------------------------------------------------------------------------------

AMFDetonationLock::AMFDetonationLock()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(3.f);   // MF NetUpdateFrequency 3

	// MF CollisionRadius 60, CollisionHeight 60 uu
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(135.f);
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AMFDetonationLock::OnOverlap);
	RootComponent = Trigger;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Trigger);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cyl.Succeeded())
	{
		Mesh->SetStaticMesh(Cyl.Object);   // placeholder until the MF lock mesh is converted
		Mesh->SetRelativeScale3D(FVector(0.5f, 0.5f, 1.4f));
	}
}

void AMFDetonationLock::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFDetonationLock, Team);
	DOREPLIFETIME(AMFDetonationLock, CountDown);
}

void AMFDetonationLock::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (UMaterialInterface* M = UMFGameSettings::Get().GetTeamTintMaterial())
		{
			Mesh->SetMaterial(0, M);
		}
		const FLinearColor C = UMFGameSettings::Get().GetTeamColor(Team);
		Mesh->SetCustomPrimitiveDataVector4(0, FVector4(C.R, C.G, C.B, 1.f));
	}
}

void AMFDetonationLock::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bSweep, const FHitResult& Hit)
{
	// MF state UnLocked.Touch: an enemy of this lock's team carrying the key arms it
	AMFCharacter* C = Cast<AMFCharacter>(Other);
	AMFGameMode_Detonation* GM = DetMode(this);
	if (!HasAuthority() || !GM || IsArmed() || !C || !C->IsAlive() || C->GetTeam() == Team || C->GetTeam() == MF::NoTeam)
	{
		return;
	}
	if (AMFFlag::FindCarriedBy(C) != GM->GetKey())
	{
		return;
	}
	Scorer = C->GetController();
	ScorerTeam = C->GetTeam();
	GM->KeyInserted(this, C);
	CountDown = 5;
	bKilling = false;
	GM->Event(FString::Printf(TEXT("Detonation in %d sec"), CountDown), ScorerTeam);
	GetWorldTimerManager().SetTimer(Timer, this, &AMFDetonationLock::Tick1, 1.f, false);
	ForceNetUpdate();
}

void AMFDetonationLock::Tick1()
{
	AMFGameMode_Detonation* GM = DetMode(this);
	if (!GM)
	{
		return;
	}
	if (!bKilling)
	{
		if (--CountDown > 0)
		{
			GM->Event(FString::Printf(TEXT("Detonation in %d sec"), CountDown), ScorerTeam);
			GetWorldTimerManager().SetTimer(Timer, this, &AMFDetonationLock::Tick1, 1.f, false);
			return;
		}
		CountDown = 0;
		bKilling = true;
		GetWorldTimerManager().SetTimer(Timer, this, &AMFDetonationLock::Tick1, FMath::FRand() + 0.01f, false);
		return;
	}
	// MF state Locked.Timer: one member of the lock's team at a time, random 0-1 s apart
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && It->GetTeam() == Team)
		{
			FDamageEvent Ev(UMFDamage_Explosion::StaticClass());
			It->TakeDamage(20000.f, Ev, Scorer.Get(), this);
			GetWorldTimerManager().SetTimer(Timer, this, &AMFDetonationLock::Tick1, FMath::FRand() + 0.01f, false);
			return;
		}
	}
	// nobody of that team alive: score, back to waiting for the key
	CountDown = -1;
	bKilling = false;
	ForceNetUpdate();
	GM->Detonated(Scorer.Get(), ScorerTeam);
}

// --- game mode -------------------------------------------------------------------------

AMFGameMode_Detonation::AMFGameMode_Detonation()
{
	GameName = TEXT("Detonation");
	GoalTeamScore = 4;      // RageDetonation.GoalTeamScore
	TimeLimit = 10;
	ObjectName = TEXT("Detonation Key");
	CarryLine = TEXT("");
}

void AMFGameMode_Detonation::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();
	for (AMFLegacyActor* L : AMFLegacyActor::FindAll(GetWorld(), TEXT("RageDetPossibleKeyPos")))
	{
		KeySpots.Add(L->GetActorTransform());
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (AMFLegacyActor* L : AMFLegacyActor::FindAll(GetWorld(), TEXT("RageDetonationLock")))
	{
		if (L->Team > 1)
		{
			continue;
		}
		AMFDetonationLock* Lock = GetWorld()->SpawnActor<AMFDetonationLock>(AMFDetonationLock::StaticClass(), L->GetActorTransform(), SP);
		if (Lock)
		{
			Lock->Team = L->Team;
			Locks.Add(Lock);
		}
	}
	if (KeySpots.Num() == 0 || Locks.Num() < 2)
	{
		UE_LOG(LogMF, Warning, TEXT("Detonation: map has %d key spots and %d locks"), KeySpots.Num(), Locks.Num());
		return;
	}
	Key = SpawnFlag(MF::NoTeam, nullptr, KeySpots[0].GetLocation());
	RestartKey();
}

void AMFGameMode_Detonation::RestartKey()
{
	if (!Key || KeySpots.Num() == 0)
	{
		return;
	}
	const FTransform& T = KeySpots[FMath::RandRange(0, KeySpots.Num() - 1)];
	Key->SetActorEnableCollision(true);
	Key->PlaceDropped(T.GetLocation(), T.Rotator());
}

void AMFGameMode_Detonation::FlagTimedOut(AMFFlag* Flag)
{
	Event(TEXT("Detonation has restarted."), 255);
	RestartKey();
}

void AMFGameMode_Detonation::FlagPickedUp(AMFFlag* Flag, AMFCharacter* By)
{
	Event(FString::Printf(TEXT("%s: key secured! Proceeding to detonation point!"), *NameOf(By)), By->GetTeam());
}

void AMFGameMode_Detonation::FlagDropped(AMFFlag* Flag, AMFCharacter* Was)
{
	Event(FString::Printf(TEXT("%s: mission abort! Detonation Key dropped!"), *NameOf(Was)), Was->GetTeam());
}

void AMFGameMode_Detonation::KeyInserted(AMFDetonationLock* Lock, AMFCharacter* By)
{
	// the key sits in the lock until the detonation is done (MF key state InUse)
	Key->SetActorEnableCollision(false);
	Key->PlaceDropped(Lock->GetActorLocation());
	Key->SetActorHiddenInGame(true);
	GetWorldTimerManager().ClearAllTimersForObject(Key);   // no 25 s restart while it's in there
}

void AMFGameMode_Detonation::Detonated(AController* Scorer, uint8 ScorerTeam)
{
	if (IsMatchInProgress())
	{
		if (AMFPlayerState* PS = Scorer ? Scorer->GetPlayerState<AMFPlayerState>() : nullptr)
		{
			PS->SetScore(PS->GetScore() + 7.f);
		}
		AMFGameState* GS = GetGameState<AMFGameState>();
		if (GS && GS->TeamScores.IsValidIndex(ScorerTeam))
		{
			GS->TeamScores[ScorerTeam] += 1;
		}
		Event(FString::Printf(TEXT("%s: %s OBJECTIVE ACHIEVED! - %s DETONATION ACTIVATED!!!"),
			Scorer && Scorer->PlayerState ? *Scorer->PlayerState->GetPlayerName() : TEXT("Someone"),
			ScorerTeam == 0 ? TEXT("BLUE") : TEXT("RED"), ScorerTeam == 0 ? TEXT("RED") : TEXT("BLUE")), ScorerTeam);
		if (GS && GoalTeamScore > 0 && GS->GetTeamScore(ScorerTeam) >= GoalTeamScore)
		{
			FinishMatch(FString::Printf(TEXT("%s team wins!"), TeamName(ScorerTeam)));
			return;
		}
	}
	RestartKey();
}

FString AMFGameMode_Detonation::GetOrderString(int32 Order) const
{
	// Rage.int OrderString 9 / 10 (Detonation uses the "point" lines)
	if (Order == 0)
	{
		return TEXT("Defend the command post.");
	}
	if (Order == 2)
	{
		return TEXT("Take the point.");
	}
	return Super::GetOrderString(Order);
}

void AMFGameMode_Detonation::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const
{
	const AMFPlayerState* PS = Bot ? Bot->GetPlayerState<AMFPlayerState>() : nullptr;
	const AMFCharacter* Me = Bot ? Cast<AMFCharacter>(Bot->GetPawn()) : nullptr;
	if (!PS || !Me || !Key)
	{
		return;
	}
	AMFDetonationLock* MyLock = nullptr;
	AMFDetonationLock* EnemyLock = nullptr;
	for (AMFDetonationLock* L : Locks)
	{
		(L->Team == PS->GetTeam() ? MyLock : EnemyLock) = L;
	}
	if (AMFFlag::FindCarriedBy(Me) == Key)
	{
		if (EnemyLock)
		{
			OutGoals.Add(EnemyLock);
		}
		return;
	}
	if (Key->State == EMFFlagState::Dropped && !Key->IsHidden())
	{
		OutGoals.Add(Key);
	}
	else if (Key->State == EMFFlagState::Held && Key->Holder)
	{
		// an enemy carries it: stop them; a team-mate: go with them to the enemy lock
		OutGoals.Add(Key->Holder->GetTeam() == PS->GetTeam() ? static_cast<AActor*>(EnemyLock) : static_cast<AActor*>(Key->Holder));
	}
	const AMFBotController* B = Cast<AMFBotController>(Bot);
	if (MyLock && B && B->GetOrder() == EMFBotOrder::Defend)
	{
		OutGoals.Insert(MyLock, 0);
	}
	else if (MyLock)
	{
		OutGoals.Add(MyLock);
	}
}
