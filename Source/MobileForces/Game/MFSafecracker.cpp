#include "Game/MFSafecracker.h"
#include "MobileForces.h"
#include "Core/MFGameSettings.h"
#include "Core/MFTypes.h"
#include "Game/MFGameState.h"
#include "Game/MFPlayerState.h"
#include "GameFramework/PlayerState.h"
#include "Player/MFCharacter.h"
#include "AI/MFBotController.h"
#include "World/MFMapActors.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* TeamName(uint8 Team) { return Team == 0 ? TEXT("Blue") : Team == 1 ? TEXT("Red") : TEXT("Neutral"); }

	FString NameOf(const APawn* P)
	{
		return P && P->GetPlayerState() ? P->GetPlayerState()->GetPlayerName() : FString(TEXT("Someone"));
	}

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
	FActorSpawnParameters SP;
	SP.Owner = this;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Gold = GetWorld()->SpawnActor<AMFGold>(AMFGold::StaticClass(), GetActorTransform(), SP);
	if (Gold)
	{
		Gold->Team = Team;
		Gold->HomeSafe = this;
		Gold->SendHome();
	}
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
	float Actual = Damage * ((Type && Type->bExplosive) ? 0.15f : 0.05f);
	Actual = FMath::Max(Actual, 1.f);
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
						M->Event(FString::Printf(TEXT("The %s safe is repaired!"), TeamName(Team)), Team);
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
	if (bOpen || !Gold || Gold->State != EMFGoldState::Home)
	{
		return;
	}
	bOpen = true;
	UpdateLook();
	if (AMFGameMode_Safecracker* GM = SCMode(this))
	{
		GM->Event(FString::Printf(TEXT("The %s safe has been blown!"), TeamName(Team)), Team);
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
	if (!bOpen || !Gold || Gold->State != EMFGoldState::Home)
	{
		return false;
	}
	bOpen = false;
	Health = 100.f;
	UpdateLook();
	return true;
}

void AMFSafe::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bSweep, const FHitResult& Hit)
{
	TouchedBy(Cast<AMFCharacter>(Other));
}

void AMFSafe::TouchedBy(AMFCharacter* C)
{
	if (!HasAuthority() || !C || !C->IsAlive() || !Gold)
	{
		return;
	}
	if (C->GetTeam() == Team)
	{
		// MF RageSafe.Touch: own safe + carrying gold = score
		for (TActorIterator<AMFGold> It(GetWorld()); It; ++It)
		{
			if (It->Holder == C)
			{
				if (AMFGameMode_Safecracker* GM = SCMode(this))
				{
					GM->ScoreSafe(C, *It);
				}
				return;
			}
		}
	}
	else if (bOpen && Gold->State == EMFGoldState::Home)
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
	FLinearColor C = UMFGameSettings::Get().GetTeamColor(Team);
	if (bOpen)
	{
		C *= 0.35f;   // blown: dark (the MF open-safe mesh until it's converted)
	}
	Mesh->SetCustomPrimitiveDataVector4(0, FVector4(C.R, C.G, C.B, 1.f));
}

// --- gold ------------------------------------------------------------------------------

AMFGold::AMFGold()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(10.f);

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(60.f);
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AMFGold::OnOverlap);
	RootComponent = Trigger;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Trigger);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Mesh->SetStaticMesh(Cube.Object);
		Mesh->SetRelativeScale3D(FVector(0.5f, 0.3f, 0.25f));   // a gold bar, placeholder
	}
}

void AMFGold::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFGold, Team);
	DOREPLIFETIME(AMFGold, State);
	DOREPLIFETIME(AMFGold, Holder);
}

void AMFGold::BeginPlay()
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

void AMFGold::SendHome()
{
	GetWorldTimerManager().ClearTimer(ReturnTimer);
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Holder = nullptr;
	State = EMFGoldState::Home;
	if (AMFSafe* Safe = HomeSafe.Get())
	{
		// inside the safe: hidden while it's shut, on show when it's blown
		SetActorLocation(Safe->GetActorLocation());
	}
	UpdateLook();
	ForceNetUpdate();
}

void AMFGold::PickUp(AMFCharacter* By)
{
	if (!By)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(ReturnTimer);
	Holder = By;
	State = EMFGoldState::Held;
	AttachToActor(By, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorRelativeLocation(FVector(-30.f, 0.f, 40.f));   // on the back
	UpdateLook();
	ForceNetUpdate();
	if (AMFGameMode_Safecracker* GM = SCMode(this))
	{
		GM->Event(FString::Printf(TEXT("%s: I have the %s gold! Transport in progress!"), *NameOf(By), TeamName(Team)), Team);
	}
	if (AMFSafe* Safe = HomeSafe.Get())
	{
		UGameplayStatics::PlaySoundAtLocation(this, LoadObject<USoundBase>(nullptr, TEXT("/Game/MFImported/Sounds/CTFSFX/flagtaken.flagtaken"), nullptr, LOAD_NoWarn | LOAD_Quiet), Safe->GetActorLocation());
	}
}

void AMFGold::Drop()
{
	AMFCharacter* Was = Holder;
	if (!Was)
	{
		return;
	}
	if (AMFGameMode_Safecracker* GM = SCMode(this))
	{
		GM->Event(FString::Printf(TEXT("%s: mission abort! %s gold dropped!"), *NameOf(Was), TeamName(Team)), Team);
	}
	OldHolder = Was;
	TimeDropped = GetWorld()->GetTimeSeconds();
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Holder = nullptr;
	State = EMFGoldState::Dropped;
	// MF Drop: behind the holder, then falls
	FVector Spot = Was->GetActorLocation() - Was->GetActorForwardVector() * 76.f;
	FHitResult Hit;
	FCollisionQueryParams Params(TEXT("MFGold"), false, Was);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Was->GetActorLocation(), Spot, ECC_WorldStatic, Params))
	{
		Spot = Was->GetActorLocation();
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, Spot, Spot - FVector(0, 0, 5000.f), ECC_WorldStatic, Params))
	{
		Spot = Hit.Location + FVector(0, 0, 20.f);
	}
	SetActorLocationAndRotation(Spot, FRotator::ZeroRotator);
	UpdateLook();
	ForceNetUpdate();
	// MF state Dropped: home again after 25 s
	GetWorldTimerManager().SetTimer(ReturnTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		SendHome();
		if (AMFGameMode_Safecracker* GM = SCMode(this))
		{
			GM->Event(FString::Printf(TEXT("The %s gold was returned!"), TeamName(Team)), Team);
		}
	}), 25.f, false);
	// anyone already standing on the spot
	TArray<AActor*> Inside;
	Trigger->GetOverlappingActors(Inside, AMFCharacter::StaticClass());
	for (AActor* A : Inside)
	{
		OnOverlap(Trigger, A, nullptr, 0, false, FHitResult());
	}
}

void AMFGold::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bSweep, const FHitResult& Hit)
{
	AMFCharacter* C = Cast<AMFCharacter>(Other);
	if (!HasAuthority() || State != EMFGoldState::Dropped || !C || !C->IsAlive() || C->GetTeam() == MF::NoTeam)
	{
		return;
	}
	if (C == OldHolder.Get() && GetWorld()->GetTimeSeconds() < TimeDropped + 1.f)
	{
		return;
	}
	if (C->GetTeam() == Team)
	{
		if (AMFGameMode_Safecracker* GM = SCMode(this))
		{
			GM->ScoreSafe(C, this);   // returned by its own team
		}
		SendHome();
		return;
	}
	PickUp(C);
}

void AMFGold::OnRep_State()
{
	UpdateLook();
}

void AMFGold::UpdateLook()
{
	// gold colour with a hint of the team; hidden in a shut safe
	const AMFSafe* Safe = HomeSafe.Get();
	SetActorHiddenInGame(State == EMFGoldState::Home && Safe && !Safe->bOpen);
	const FLinearColor T = UMFGameSettings::Get().GetTeamColor(Team);
	const FLinearColor C = FLinearColor(1.f, 0.78f, 0.15f) * 0.8f + T * 0.2f;
	Mesh->SetCustomPrimitiveDataVector4(0, FVector4(C.R, C.G, C.B, 1.f));
}

// --- game mode -------------------------------------------------------------------------

AMFGameMode_Safecracker::AMFGameMode_Safecracker()
{
	GameName = TEXT("SafeCracker");
	NumTeams = 2;
	TimeLimit = 9;          // RageSafeCracker.TimeLimit
	FragLimit = 0;
	GoalTeamScore = 1;
	RespawnWait = 5;        // RespawnWait / MissionRespawnWait
	bScoreTeamKills = false;
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
		if (Safe)
		{
			Safe->Init(L->Team, L->Team == DefenderTeam());
			Safes.Add(Safe);
		}
	}
	if (Safes.Num() < 2)
	{
		UE_LOG(LogMF, Warning, TEXT("Safecracker: map has %d RageSafe(s), needs one per team"), Safes.Num());
	}
}

void AMFGameMode_Safecracker::Event(const FString& Text, uint8 ColourTeam)
{
	UE_LOG(LogMF, Display, TEXT("Safecracker: %s"), *Text);
	const FName Type(*FString::Printf(TEXT("Event%d"), ColourTeam));
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			PC->ClientTeamMessage(nullptr, Text, Type);
		}
	}
}

void AMFGameMode_Safecracker::Killed(AController* Killer, AController* Victim, AMFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType)
{
	// MF RageSafeCracker.Killed: the carrier drops the gold; killing them is worth 4
	for (TActorIterator<AMFGold> It(GetWorld()); It; ++It)
	{
		if (It->Holder == VictimPawn)
		{
			AMFPlayerState* KPS = Killer ? Killer->GetPlayerState<AMFPlayerState>() : nullptr;
			const AMFPlayerState* VPS = Victim ? Victim->GetPlayerState<AMFPlayerState>() : nullptr;
			if (KPS && VPS && KPS != VPS && KPS->GetTeam() != VPS->GetTeam())
			{
				KPS->SetScore(KPS->GetScore() + 4.f);
			}
			It->Drop();
		}
	}
	Super::Killed(Killer, Victim, VictimPawn, DamageType);
}

void AMFGameMode_Safecracker::ScoreSafe(AMFCharacter* Scorer, AMFGold* Gold)
{
	if (!Scorer || !Gold || !IsMatchInProgress())
	{
		return;
	}
	if (Scorer->GetTeam() == Gold->Team)
	{
		Event(FString::Printf(TEXT("%s: %s gold has been returned to %s base!"), *NameOf(Scorer), TeamName(Gold->Team), TeamName(Gold->Team)), Gold->Team);
		return;
	}
	// stolen gold delivered
	AMFPlayerState* PS = Scorer->GetPlayerState<AMFPlayerState>();
	if (PS)
	{
		PS->SetScore(PS->GetScore() + 7.f);
	}
	if (AMFGameState* GS = GetGameState<AMFGameState>(); GS && GS->TeamScores.IsValidIndex(Scorer->GetTeam()))
	{
		GS->TeamScores[Scorer->GetTeam()] += 1;
	}
	Event(FString::Printf(TEXT("%s: %s OBJECTIVE ACHIEVED! - %s GOLD STOLEN!!!"), *NameOf(Scorer),
		Scorer->GetTeam() == 0 ? TEXT("BLUE") : TEXT("RED"), Gold->Team == 0 ? TEXT("BLUE") : TEXT("RED")), Scorer->GetTeam());
	Gold->SendHome();
	bSafeCracked = true;
	const AMFGameState* GS = GetGameState<AMFGameState>();
	const int32 Used = GS ? (GS->TimeLimitSeconds > 0 ? GS->TimeLimitSeconds : GS->TimeLimit * 60) - GS->GetRemainingTime() : 0;
	FirstCaptureTime = Part == 1 ? Used : FirstCaptureTime;
	FinishMatch(FString::Printf(TEXT("%s stole the gold in %d:%02d%s"), TeamName(Scorer->GetTeam()), Used / 60, Used % 60,
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
	if (Part == 1)
	{
		// MF RestartGame: part 2 on the same map, sides swapped, part 1's time to beat
		FString Opts = OptionsString;
		Opts = Opts.Replace(TEXT("?Part=1"), TEXT(""));
		GetWorld()->ServerTravel(FString::Printf(TEXT("%s%s?Part=2?FirstCaptureTime=%d"), *GetWorld()->GetOutermost()->GetName(), *Opts, FirstCaptureTime), false);
		return;
	}
	FString Opts = OptionsString;
	for (const TCHAR* Key : { TEXT("Part"), TEXT("FirstCaptureTime") })
	{
		const FString V = UGameplayStatics::ParseOption(Opts, Key);
		Opts = Opts.Replace(*FString::Printf(TEXT("?%s=%s"), Key, *V), TEXT(""));
	}
	GetWorld()->ServerTravel(GetWorld()->GetOutermost()->GetName() + Opts, false);
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
	for (TActorIterator<AMFGold> It(GetWorld()); It; ++It)
	{
		if (It->Holder == Me && MySafe)
		{
			OutGoals.Add(MySafe);
			return;
		}
	}
	// dropped gold nearby: grab the enemy's, return ours
	for (TActorIterator<AMFGold> It(GetWorld()); It; ++It)
	{
		if (It->State == EMFGoldState::Dropped)
		{
			OutGoals.Add(*It);
		}
	}
	const bool bAttacker = MyTeam == AttackerTeam();
	const EMFBotOrder Order = [Bot]()
	{
		const AMFBotController* B = Cast<AMFBotController>(Bot);
		return B ? B->GetOrder() : EMFBotOrder::Freelance;
	}();
	if (bAttacker && Order != EMFBotOrder::Defend)
	{
		if (EnemySafe)
		{
			OutGoals.Add(EnemySafe);   // crack it, then take the gold
		}
	}
	else if (MySafe)
	{
		// defenders (and attackers told to defend): guard the safe; chase a carrier through FindEnemy
		for (TActorIterator<AMFGold> It(GetWorld()); It; ++It)
		{
			if (It->Team == MyTeam && It->State == EMFGoldState::Held && It->Holder)
			{
				OutGoals.Add(It->Holder);
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
