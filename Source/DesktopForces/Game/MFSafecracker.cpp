#include "Game/MFSafecracker.h"
#include "Core/MFProps.h"
#include "DesktopForces.h"
#include "AI/MFBotController.h"
#include "Core/MFGameSettings.h"
#include "Core/MFTypes.h"
#include "Game/MFGameState.h"
#include "Game/MFPlayerState.h"
#include "Player/MFCharacter.h"
#include "World/MFMapActors.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	AMFGameMode_Safecracker* SCMode(const UObject* Ctx)
	{
		return Ctx->GetWorld() ? Ctx->GetWorld()->GetAuthGameMode<AMFGameMode_Safecracker>() : nullptr;
	}
}

// --- safe ------------------------------------------------------------------------------

AMFSafe::AMFSafe()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;   // on everyone's HUD (safe meter) and minimap
	SetNetUpdateFrequency(4.f);

	// MF CollisionRadius 40, CollisionHeight 40 uu; blocks players and shots
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->InitBoxExtent(FVector(90.f, 90.f, 90.f));
	Body->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	RootComponent = Body;

	// slightly bigger than the body so walking into it counts as touching (MF Touch / Bump)
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(150.f);
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->SetupAttachment(Body);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AMFSafe::OnOverlap);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Body);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Mesh->SetStaticMesh(Cube.Object);
		Mesh->SetRelativeScale3D(FVector(1.8f));   // placeholder until the MF safe mesh is converted
	}
}

void AMFSafe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFSafe, Team);
	DOREPLIFETIME(AMFSafe, Health);
	DOREPLIFETIME(AMFSafe, bOpen);
	DOREPLIFETIME(AMFSafe, bCrackable);
	DOREPLIFETIME(AMFSafe, Gold);
}

void AMFSafe::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (UMaterialInterface* M = UMFGameSettings::Get().GetTeamTintMaterial())
		{
			Mesh->SetMaterial(0, M);
		}
		UpdateLook();
	}
}

void AMFSafe::Init(uint8 InTeam, bool bInCrackable)
{
	Team = InTeam;
	bCrackable = bInCrackable;
	bOpen = true;   // MF PostBeginPlay: bOpen = true; CloseSafe()
	CloseSafe();
	UpdateLook();
}

float AMFSafe::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// MF RageSafe.TakeDamage: only the defending team's safe, only enemies, scaled down hard
	if (!HasAuthority() || !bCrackable || bOpen || Health < 0.f)
	{
		return 0.f;
	}
	const AMFPlayerState* PS = EventInstigator ? EventInstigator->GetPlayerState<AMFPlayerState>() : nullptr;
	if (PS && PS->GetTeam() == Team)
	{
		return 0.f;
	}
	const UMFDamageType* Type = Cast<UMFDamageType>(DamageEvent.DamageTypeClass ? DamageEvent.DamageTypeClass->GetDefaultObject() : nullptr);
	const float Actual = FMath::Max(Damage * ((Type && Type->bExplosive) ? 0.15f : 0.05f), 1.f);
	Health -= Actual;
	if (Health < 0.f)
	{
		if (const AMFGameMode_Safecracker* GM = SCMode(this); GM && GM->AutoCloseTime > 0)
		{
			GetWorldTimerManager().SetTimer(CloseTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (CloseSafe())
				{
					if (AMFGameMode_Safecracker* M = SCMode(this))
					{
						M->Event(FString::Printf(TEXT("The %s safe is repaired!"), AMFGameMode_Flags::TeamName(Team)), Team);
					}
					GetWorldTimerManager().ClearTimer(CloseTimer);
				}
			}), float(GM->AutoCloseTime), true);
		}
		OpenSafe();
	}
	ForceNetUpdate();
	return Actual;
}

void AMFSafe::OpenSafe()
{
	if (bOpen || !Gold || Gold->State != EMFFlagState::Home)
	{
		return;
	}
	bOpen = true;
	Gold->bHiddenAtHome = false;
	Gold->UpdateLook();
	UpdateLook();
	if (AMFGameMode_Safecracker* GM = SCMode(this))
	{
		GM->Event(FString::Printf(TEXT("The %s safe has been blown!"), AMFGameMode_Flags::TeamName(Team)), Team);
	}
	// someone may already stand at it
	TArray<AActor*> Inside;
	Trigger->GetOverlappingActors(Inside, AMFCharacter::StaticClass());
	for (AActor* A : Inside)
	{
		TouchedBy(Cast<AMFCharacter>(A));
	}
}

bool AMFSafe::CloseSafe()
{
	if (!bOpen || !Gold || Gold->State != EMFFlagState::Home)
	{
		return false;
	}
	bOpen = false;
	Health = 100.f;
	Gold->bHiddenAtHome = true;   // inside the shut safe
	Gold->UpdateLook();
	UpdateLook();
	return true;
}

void AMFSafe::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bSweep, const FHitResult& Hit)
{
	TouchedBy(Cast<AMFCharacter>(Other));
}

void AMFSafe::TouchedBy(AMFCharacter* C)
{
	AMFGameMode_Safecracker* GM = SCMode(this);
	if (!HasAuthority() || !GM || !C || !C->IsAlive() || !Gold)
	{
		return;
	}
	if (C->GetTeam() == Team)
	{
		// MF RageSafe.Touch: own safe + carrying the enemy gold = score
		if (AMFFlag* Carried = AMFFlag::FindCarriedBy(C); Carried && Carried != Gold)
		{
			GM->FlagCaptured(Carried, C);
		}
	}
	else if (bOpen && Gold->State == EMFFlagState::Home)
	{
		Gold->PickUp(C);
	}
}

void AMFSafe::OnRep_Open()
{
	UpdateLook();
}

void AMFSafe::UpdateLook()
{
	// MF RageSafe meshes per team, the blown-open one once cracked
	const TCHAR* Prop = bOpen ? (Team == 1 ? TEXT("RageGame/RageSafeOpenMeshRed") : TEXT("RageGame/RageSafeOpenMeshBlue"))
		: (Team == 1 ? TEXT("RageGame/RageSafeMeshRed") : TEXT("RageGame/RageSafeMeshBlue"));
	if (MF::ApplyPropMesh(Mesh, Prop))
	{
		return;
	}
	FLinearColor C = UMFGameSettings::Get().GetTeamColor(Team);
	if (bOpen)
	{
		C *= 0.35f;   // blown: dark (the MF open-safe mesh until it's converted)
	}
	Mesh->SetCustomPrimitiveDataVector4(0, FVector4(C.R, C.G, C.B, 1.f));
}

// --- game mode -------------------------------------------------------------------------

AMFGameMode_Safecracker::AMFGameMode_Safecracker()
{
	GameName = TEXT("SafeCracker");
	TimeLimit = 9;          // RageSafeCracker.TimeLimit
	GoalTeamScore = 1;
	RespawnWait = 5;        // RespawnWait / MissionRespawnWait
	ObjectName = TEXT("gold");
	CarryLine = TEXT("Transport in progress!");
}

void AMFGameMode_Safecracker::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	Part = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Part"), 1), 1, 2);
	FirstCaptureTime = UGameplayStatics::GetIntOption(Options, TEXT("FirstCaptureTime"), 0);
	if (TimeLimit < 1)
	{
		TimeLimit = 8;      // RageSafeCracker.InitGame
	}
	if (Part == 2 && FirstCaptureTime > 0)
	{
		TimeLimitSeconds = FirstCaptureTime;   // beat part 1's time
	}
}

void AMFGameMode_Safecracker::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();
	SpawnSafes();
	Event(FString::Printf(TEXT("Part %d: %s team attacks, %s team defends its gold."), Part,
		TeamName(AttackerTeam()), TeamName(DefenderTeam())), 255);
}

void AMFGameMode_Safecracker::SpawnSafes()
{
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (AMFLegacyActor* L : AMFLegacyActor::FindAll(GetWorld(), TEXT("RageSafe")))
	{
		if (L->Team > 1)
		{
			continue;
		}
		AMFSafe* Safe = GetWorld()->SpawnActor<AMFSafe>(AMFSafe::StaticClass(), L->GetActorTransform(), SP);
		if (!Safe)
		{
			continue;
		}
		Safe->Gold = SpawnFlag(L->Team, Safe, Safe->GetActorLocation());
		if (Safe->Gold)
		{
			Safe->Gold->bHiddenAtHome = true;
		}
		Safe->Init(L->Team, L->Team == DefenderTeam());
		Safes.Add(Safe);
	}
	if (Safes.Num() < 2)
	{
		UE_LOG(LogMF, Warning, TEXT("Safecracker: map has %d RageSafe(s), needs one per team"), Safes.Num());
	}
}

void AMFGameMode_Safecracker::FlagTouchedAtHome(AMFFlag* Flag, AMFCharacter* C)
{
	// the gold at home is inside its safe: taken only once the safe is blown
	for (AMFSafe* S : Safes)
	{
		if (S->Gold == Flag)
		{
			S->TouchedBy(C);
		}
	}
}

void AMFGameMode_Safecracker::FlagCaptured(AMFFlag* Flag, AMFCharacter* By)
{
	if (!IsMatchInProgress())
	{
		return;
	}
	Super::FlagCaptured(Flag, By);   // +7, team point, the line, gold home
	const AMFGameState* GS = GetGameState<AMFGameState>();
	const int32 Used = GS ? (GS->TimeLimitSeconds > 0 ? GS->TimeLimitSeconds : GS->TimeLimit * 60) - GS->GetRemainingTime() : 0;
	if (Part == 1)
	{
		FirstCaptureTime = Used;
	}
	FinishMatch(FString::Printf(TEXT("%s stole the gold in %d:%02d%s"), TeamName(By->GetTeam()), Used / 60, Used % 60,
		Part == 2 ? TEXT(" and win!") : TEXT("")));
}

void AMFGameMode_Safecracker::OnTimeLimit()
{
	// MF SetEndCams: nobody delivered the gold in time
	if (Part == 1)
	{
		FirstCaptureTime = 0;
	}
	if (AMFGameState* GS = GetGameState<AMFGameState>(); GS && GS->TeamScores.IsValidIndex(DefenderTeam()))
	{
		GS->TeamScores[DefenderTeam()] += 1;
	}
	FinishMatch(FString::Printf(TEXT("%s safeguarded their gold%s"), TeamName(DefenderTeam()), Part == 2 ? TEXT(" and win!") : TEXT("!")));
}

void AMFGameMode_Safecracker::RestartGame()
{
	const FString Map = GetWorld()->GetOutermost()->GetName();
	FString Opts = OptionsString;
	for (const TCHAR* Key : { TEXT("Part"), TEXT("FirstCaptureTime") })
	{
		const FString V = UGameplayStatics::ParseOption(Opts, Key);
		Opts = Opts.Replace(*FString::Printf(TEXT("?%s=%s"), Key, *V), TEXT(""));
	}
	if (Part == 1)
	{
		// MF RestartGame: part 2 on the same map, sides swapped, part 1's time to beat
		GetWorld()->ServerTravel(FString::Printf(TEXT("%s%s?Part=2?FirstCaptureTime=%d"), *Map, *Opts, FirstCaptureTime), false);
		return;
	}
	GetWorld()->ServerTravel(Map + Opts, false);
}

FString AMFGameMode_Safecracker::GetOrderString(int32 Order) const
{
	// Rage.int OrderString 11 / 12
	if (Order == 0)
	{
		return TEXT("Guard the safe.");
	}
	if (Order == 2)
	{
		return TEXT("Grab the gold.");
	}
	return Super::GetOrderString(Order);
}

void AMFGameMode_Safecracker::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const
{
	const AMFPlayerState* PS = Bot ? Bot->GetPlayerState<AMFPlayerState>() : nullptr;
	const AMFCharacter* Me = Bot ? Cast<AMFCharacter>(Bot->GetPawn()) : nullptr;
	if (!PS || !Me)
	{
		return;
	}
	const uint8 MyTeam = PS->GetTeam();
	AMFSafe* MySafe = nullptr;
	AMFSafe* EnemySafe = nullptr;
	for (AMFSafe* S : Safes)
	{
		(S->Team == MyTeam ? MySafe : EnemySafe) = S;
	}
	// carrying the gold: home (MF PriorityObjective 255 near home)
	if (AMFFlag::FindCarriedBy(Me))
	{
		if (MySafe)
		{
			OutGoals.Add(MySafe);
		}
		return;
	}
	// dropped gold: grab the enemy's, return ours
	for (AMFFlag* F : Flags)
	{
		if (F->State == EMFFlagState::Dropped)
		{
			OutGoals.Add(F);
		}
	}
	const AMFBotController* B = Cast<AMFBotController>(Bot);
	const EMFBotOrder Order = B ? B->GetOrder() : EMFBotOrder::Freelance;
	if (MyTeam == AttackerTeam() && Order != EMFBotOrder::Defend)
	{
		if (EnemySafe)
		{
			OutGoals.Add(EnemySafe);   // crack it, then take the gold
		}
	}
	else if (MySafe)
	{
		// defenders (and attackers told to defend): chase our gold's carrier, else guard the safe
		for (AMFFlag* F : Flags)
		{
			if (F->Team == MyTeam && F->State == EMFFlagState::Held && F->Holder)
			{
				OutGoals.Add(F->Holder);
			}
		}
		OutGoals.Add(MySafe);
	}
}

AActor* AMFGameMode_Safecracker::GetBotShootTarget(AController* Bot) const
{
	// MF RageSafePawn: bots shoot the enemy's closed, crackable safe when they see it
	const AMFPlayerState* PS = Bot ? Bot->GetPlayerState<AMFPlayerState>() : nullptr;
	const APawn* Me = Bot ? Bot->GetPawn() : nullptr;
	if (!PS || !Me || PS->GetTeam() != AttackerTeam())
	{
		return nullptr;
	}
	for (AMFSafe* S : Safes)
	{
		if (S->bCrackable && !S->bOpen && S->Team != PS->GetTeam()
			&& FVector::Dist(S->GetActorLocation(), Me->GetActorLocation()) < 2000.f && Bot->LineOfSightTo(S))
		{
			return S;
		}
	}
	return nullptr;
}
