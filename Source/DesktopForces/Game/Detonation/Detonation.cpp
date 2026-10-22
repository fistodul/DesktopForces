#include "Game/Detonation/Detonation.h"
#include "Core/DFProps.h"
#include "DesktopForces.h"
#include "AI/DFBotController.h"
#include "Core/DFGameSettings.h"
#include "Core/DFTypes.h"
#include "Game/DFGameState.h"
#include "Game/DFPlayerState.h"
#include "Player/DFCharacter.h"
#include "World/DFMapActors.h"
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
	ADetonation* DetMode(const UObject* Ctx)
	{
		return Ctx->GetWorld() ? Ctx->GetWorld()->GetAuthGameMode<ADetonation>() : nullptr;
	}
}

// --- lock --------------------------------------------------------------------------------

ADetonationLock::ADetonationLock()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(3.f);   // MF NetUpdateFrequency 3

	// MF CollisionRadius 60, CollisionHeight 60 uu
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(135.f);
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ADetonationLock::OnOverlap);
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

void ADetonationLock::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADetonationLock, Team);
	DOREPLIFETIME(ADetonationLock, CountDown);
}

void ADetonationLock::BeginPlay()
{
	Super::BeginPlay();
	if (DF::ApplyPropMesh(Mesh, TEXT("RageGame/RageDetLockMesh")))
	{
		return;
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (UMaterialInterface* M = UDFGameSettings::Get().GetTeamTintMaterial())
		{
			Mesh->SetMaterial(0, M);
		}
		const FLinearColor C = UDFGameSettings::Get().GetTeamColor(Team);
		Mesh->SetCustomPrimitiveDataVector4(0, FVector4(C.R, C.G, C.B, 1.f));
	}
}

void ADetonationLock::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bSweep, const FHitResult& Hit)
{
	// MF state UnLocked.Touch: an enemy of this lock's team carrying the key arms it
	ADFCharacter* C = Cast<ADFCharacter>(Other);
	ADetonation* GM = DetMode(this);
	if (!HasAuthority() || !GM || IsArmed() || !C || !C->IsAlive() || C->GetTeam() == Team || C->GetTeam() == DF::NoTeam)
	{
		return;
	}
	if (AFlag::FindCarriedBy(C) != GM->GetKey())
	{
		return;
	}
	Scorer = C->GetController();
	ScorerTeam = C->GetTeam();
	GM->KeyInserted(this, C);
	CountDown = 5;
	bKilling = false;
	GM->Event(FString::Printf(TEXT("Detonation in %d sec"), CountDown), ScorerTeam, TEXT("Detonation_imminent"));   // RageDetMessage 6
	GetWorldTimerManager().SetTimer(Timer, this, &ADetonationLock::Tick1, 1.f, false);
	ForceNetUpdate();
}

void ADetonationLock::Tick1()
{
	ADetonation* GM = DetMode(this);
	if (!GM)
	{
		return;
	}
	if (!bKilling)
	{
		if (--CountDown > 0)
		{
			GM->Event(FString::Printf(TEXT("Detonation in %d sec"), CountDown), ScorerTeam, FName(*FString::FromInt(CountDown)));   // 5: the seconds
			GetWorldTimerManager().SetTimer(Timer, this, &ADetonationLock::Tick1, 1.f, false);
			return;
		}
		CountDown = 0;
		bKilling = true;
		GetWorldTimerManager().SetTimer(Timer, this, &ADetonationLock::Tick1, FMath::FRand() + 0.01f, false);
		return;
	}
	// MF state Locked.Timer: one member of the lock's team at a time, random 0-1 s apart
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && It->GetTeam() == Team)
		{
			FDamageEvent Ev(UDFDamage_Explosion::StaticClass());
			It->TakeDamage(20000.f, Ev, Scorer.Get(), this);
			GetWorldTimerManager().SetTimer(Timer, this, &ADetonationLock::Tick1, FMath::FRand() + 0.01f, false);
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

ADetonation::ADetonation()
{
	GameName = TEXT("Detonation");
	MFGameClass = TEXT("RageDetonation");
	InstructionSound = TEXT("Get_the_key");
	GoalTeamScore = 4;      // RageDetonation.GoalTeamScore
	TimeLimit = 10;
	RespawnWait = 5;        // the community servers' MobileForces.ini
	ObjectName = TEXT("Detonation Key");
	CarryLine = TEXT("");
}

void ADetonation::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();
	for (ADFLegacyActor* L : ADFLegacyActor::FindAll(GetWorld(), TEXT("RageDetPossibleKeyPos")))
	{
		KeySpots.Add(L->GetActorTransform());
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (ADFLegacyActor* L : ADFLegacyActor::FindAll(GetWorld(), TEXT("RageDetonationLock")))
	{
		if (L->Team > 1)
		{
			continue;
		}
		ADetonationLock* Lock = GetWorld()->SpawnActor<ADetonationLock>(ADetonationLock::StaticClass(), L->GetActorTransform(), SP);
		if (Lock)
		{
			Lock->Team = L->Team;
			Locks.Add(Lock);
		}
	}
	if (KeySpots.Num() == 0 || Locks.Num() < 2)
	{
		UE_LOG(LogDF, Warning, TEXT("Detonation: map has %d key spots and %d locks"), KeySpots.Num(), Locks.Num());
		return;
	}
	Key = SpawnFlag(DF::NoTeam, nullptr, KeySpots[0].GetLocation());
	RestartKey();
}

void ADetonation::RestartKey()
{
	if (!Key || KeySpots.Num() == 0)
	{
		return;
	}
	const FTransform& T = KeySpots[FMath::RandRange(0, KeySpots.Num() - 1)];
	Key->SetActorEnableCollision(true);
	Key->PlaceDropped(T.GetLocation(), T.Rotator());
}

void ADetonation::FlagTimedOut(AFlag* Flag)
{
	Event(TEXT("Detonation has restarted."), 255);
	RestartKey();
}

void ADetonation::FlagPickedUp(AFlag* Flag, ADFCharacter* By)
{
	Event(FString::Printf(TEXT("%s: key secured! Proceeding to detonation point!"), *NameOf(By)), By->GetTeam(), TEXT("key_captured"));   // RageDetMessage 4
}

void ADetonation::FlagDropped(AFlag* Flag, ADFCharacter* Was)
{
	Event(FString::Printf(TEXT("%s: mission abort! Detonation Key dropped!"), *NameOf(Was)), Was->GetTeam());
}

void ADetonation::KeyInserted(ADetonationLock* Lock, ADFCharacter* By)
{
	// the key sits in the lock until the detonation is done (MF key state InUse)
	Key->SetActorEnableCollision(false);
	Key->PlaceDropped(Lock->GetActorLocation());
	Key->SetActorHiddenInGame(true);
	GetWorldTimerManager().ClearAllTimersForObject(Key);   // no 25 s restart while it's in there
}

void ADetonation::Detonated(AController* Scorer, uint8 ScorerTeam)
{
	if (IsMatchInProgress())
	{
		if (ADFPlayerState* PS = Scorer ? Scorer->GetPlayerState<ADFPlayerState>() : nullptr)
		{
			PS->SetScore(PS->GetScore() + 7.f);
		}
		ADFGameState* GS = GetGameState<ADFGameState>();
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

FString ADetonation::GetOrderString(int32 Order) const
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

void ADetonation::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const
{
	const ADFPlayerState* PS = Bot ? Bot->GetPlayerState<ADFPlayerState>() : nullptr;
	const ADFCharacter* Me = Bot ? Cast<ADFCharacter>(Bot->GetPawn()) : nullptr;
	if (!PS || !Me || !Key)
	{
		return;
	}
	ADetonationLock* MyLock = nullptr;
	ADetonationLock* EnemyLock = nullptr;
	for (ADetonationLock* L : Locks)
	{
		(L->Team == PS->GetTeam() ? MyLock : EnemyLock) = L;
	}
	if (AFlag::FindCarriedBy(Me) == Key)
	{
		if (EnemyLock)
		{
			OutGoals.Add(EnemyLock);
		}
		return;
	}
	if (Key->State == EDFFlagState::Dropped && !Key->IsHidden())
	{
		OutGoals.Add(Key);
	}
	else if (Key->State == EDFFlagState::Held && Key->Holder)
	{
		// an enemy carries it: stop them; a team-mate: go with them to the enemy lock
		OutGoals.Add(Key->Holder->GetTeam() == PS->GetTeam() ? static_cast<AActor*>(EnemyLock) : static_cast<AActor*>(Key->Holder));
	}
	const ADFBotController* B = Cast<ADFBotController>(Bot);
	if (MyLock && B && B->GetOrder() == EDFBotOrder::Defend)
	{
		OutGoals.Insert(MyLock, 0);
	}
	else if (MyLock)
	{
		OutGoals.Add(MyLock);
	}
}
