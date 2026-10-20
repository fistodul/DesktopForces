#include "Game/MFFlag.h"
#include "Core/MFProps.h"
#include "MobileForces.h"
#include "AI/MFBotController.h"
#include "Core/MFGameSettings.h"
#include "Game/MFGameState.h"
#include "Game/MFPlayerState.h"
#include "Player/MFCharacter.h"
#include "World/MFMapActors.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	AMFGameMode_Flags* FlagMode(const UObject* Ctx)
	{
		return Ctx->GetWorld() ? Ctx->GetWorld()->GetAuthGameMode<AMFGameMode_Flags>() : nullptr;
	}
}

// --- flag --------------------------------------------------------------------------------

AMFFlag::AMFFlag()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;   // on everyone's HUD and minimap
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(10.f);

	// MF RageBlueFlag CollisionRadius 48 uu
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(108.f);
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AMFFlag::OnOverlap);
	RootComponent = Trigger;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Trigger);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Mesh->SetStaticMesh(Cube.Object);   // placeholder until the MF flag / gold meshes are converted
		Mesh->SetRelativeScale3D(FVector(0.08f, 0.6f, 0.4f));
		Mesh->SetRelativeLocation(FVector(0.f, 0.f, 40.f));
	}
}

void AMFFlag::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFFlag, Team);
	DOREPLIFETIME(AMFFlag, State);
	DOREPLIFETIME(AMFFlag, Holder);
	DOREPLIFETIME(AMFFlag, HomeBase);
	DOREPLIFETIME(AMFFlag, bHiddenAtHome);
}

void AMFFlag::BeginPlay()
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

AMFFlag* AMFFlag::FindCarriedBy(const AMFCharacter* C)
{
	if (C)
	{
		for (TActorIterator<AMFFlag> It(C->GetWorld()); It; ++It)
		{
			if (It->Holder == C)
			{
				return *It;
			}
		}
	}
	return nullptr;
}

void AMFFlag::SendHome()
{
	GetWorldTimerManager().ClearTimer(ReturnTimer);
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Holder = nullptr;
	State = EMFFlagState::Home;
	if (HomeBase)
	{
		SetActorLocationAndRotation(HomeBase->GetActorLocation(), HomeBase->GetActorRotation());
	}
	UpdateLook();
	ForceNetUpdate();
	if (AMFGameMode_Flags* GM = FlagMode(this))
	{
		GM->FlagWentHome(this);
	}
	// someone may stand at the base already
	TArray<AActor*> Inside;
	Trigger->GetOverlappingActors(Inside, AMFCharacter::StaticClass());
	for (AActor* A : Inside)
	{
		OnOverlap(Trigger, A, nullptr, 0, false, FHitResult());
	}
}

void AMFFlag::PickUp(AMFCharacter* By)
{
	if (!By || !By->IsAlive())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(ReturnTimer);
	Holder = By;
	State = EMFFlagState::Held;
	AttachToActor(By, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorRelativeLocation(FVector(-30.f, 0.f, 20.f));   // on the back
	UpdateLook();
	ForceNetUpdate();
	if (AMFGameMode_Flags* GM = FlagMode(this))
	{
		GM->FlagPickedUp(this, By);
	}
}

void AMFFlag::Drop()
{
	AMFCharacter* Was = Holder;
	if (!Was)
	{
		return;
	}
	OldHolder = Was;
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Holder = nullptr;
	// MF Drop: two collision radii behind the holder, then it falls
	FVector Spot = Was->GetActorLocation() - Was->GetActorForwardVector() * 76.f;
	FHitResult Hit;
	FCollisionQueryParams Params(TEXT("MFFlag"), false, Was);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Was->GetActorLocation(), Spot, ECC_WorldStatic, Params))
	{
		Spot = Was->GetActorLocation();
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, Spot, Spot - FVector(0, 0, 5000.f), ECC_WorldStatic, Params))
	{
		Spot = Hit.Location + FVector(0, 0, 20.f);
	}
	if (AMFGameMode_Flags* GM = FlagMode(this))
	{
		GM->FlagDropped(this, Was);
	}
	PlaceDropped(Spot);
}

void AMFFlag::PlaceDropped(const FVector& Spot, const FRotator& Rot)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Holder = nullptr;
	State = EMFFlagState::Dropped;
	TimeDropped = GetWorld()->GetTimeSeconds();
	SetActorLocationAndRotation(Spot, Rot);
	UpdateLook();
	ForceNetUpdate();
	// MF state Dropped: 25 s untouched and it goes home (the mode decides where)
	GetWorldTimerManager().SetTimer(ReturnTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (AMFGameMode_Flags* GM = FlagMode(this))
		{
			GM->FlagTimedOut(this);
		}
		else
		{
			SendHome();
		}
	}), 25.f, false);
	TArray<AActor*> Inside;
	Trigger->GetOverlappingActors(Inside, AMFCharacter::StaticClass());
	for (AActor* A : Inside)
	{
		OnOverlap(Trigger, A, nullptr, 0, false, FHitResult());
	}
}

void AMFFlag::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bSweep, const FHitResult& Hit)
{
	AMFCharacter* C = Cast<AMFCharacter>(Other);
	AMFGameMode_Flags* GM = FlagMode(this);
	if (!HasAuthority() || !GM || !C || !C->IsAlive() || C->GetTeam() == MF::NoTeam)
	{
		return;
	}
	if (State == EMFFlagState::Home)
	{
		GM->FlagTouchedAtHome(this, C);
		return;
	}
	if (State != EMFFlagState::Dropped || (C == OldHolder.Get() && GetWorld()->GetTimeSeconds() < TimeDropped + 1.f))
	{
		return;
	}
	if (C->GetTeam() == Team)
	{
		GM->FlagReturned(this, C);
		SendHome();
		return;
	}
	PickUp(C);
}

void AMFFlag::OnRep_State()
{
	UpdateLook();
}

void AMFFlag::UpdateLook()
{
	SetActorHiddenInGame(State == EMFFlagState::Home && bHiddenAtHome);
	// MF models: the detonation key (RageDetonationKey), Safecracker's gold (RageSafeObject)
	const TCHAR* Prop = Team == MF::NoTeam ? TEXT("RageGame/RageDetKeyMesh")
		: bHiddenAtHome ? (Team == 1 ? TEXT("RageGame/SafeObjRedT") : TEXT("RageGame/SafeObjBlueT")) : nullptr;
	if (Prop && MF::ApplyPropMesh(Mesh, Prop, 1.f, FVector(0.f, 0.f, 30.f)))
	{
		return;
	}
	const FLinearColor C = UMFGameSettings::Get().GetTeamColor(Team);
	Mesh->SetCustomPrimitiveDataVector4(0, FVector4(C.R, C.G, C.B, 1.f));
}

// --- flag modes --------------------------------------------------------------------------

AMFGameMode_Flags::AMFGameMode_Flags()
{
	NumTeams = 2;
	FragLimit = 0;
	RespawnWait = 5;
	bScoreTeamKills = false;
}

const TCHAR* AMFGameMode_Flags::TeamName(uint8 Team)
{
	return Team == 0 ? TEXT("Blue") : Team == 1 ? TEXT("Red") : TEXT("Neutral");
}

FString AMFGameMode_Flags::NameOf(const APawn* P)
{
	return P && P->GetPlayerState() ? P->GetPlayerState()->GetPlayerName() : FString(TEXT("Someone"));
}

AMFFlag* AMFGameMode_Flags::SpawnFlag(uint8 Team, AActor* Home, const FVector& Location)
{
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMFFlag* F = GetWorld()->SpawnActor<AMFFlag>(AMFFlag::StaticClass(), FTransform(Location), SP);
	if (F)
	{
		F->Team = Team;
		F->HomeBase = Home;
		F->SendHome();
		Flags.Add(F);
	}
	return F;
}

void AMFGameMode_Flags::Killed(AController* Killer, AController* Victim, AMFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType)
{
	// MF RageCTF / RageSafeCracker.Killed: the carrier drops it; killing them is worth 4
	if (AMFFlag* F = AMFFlag::FindCarriedBy(VictimPawn))
	{
		AMFPlayerState* KPS = Killer ? Killer->GetPlayerState<AMFPlayerState>() : nullptr;
		const AMFPlayerState* VPS = Victim ? Victim->GetPlayerState<AMFPlayerState>() : nullptr;
		if (KPS && VPS && KPS != VPS && KPS->GetTeam() != VPS->GetTeam())
		{
			KPS->SetScore(KPS->GetScore() + 4.f);
		}
		F->Drop();
	}
	Super::Killed(Killer, Victim, VictimPawn, DamageType);
}

void AMFGameMode_Flags::FlagPickedUp(AMFFlag* Flag, AMFCharacter* By)
{
	Event(FString::Printf(TEXT("%s: I have the %s %s! %s"), *NameOf(By), TeamName(Flag->Team), *ObjectName, *CarryLine), Flag->Team);
	if (Flag->HomeBase)
	{
		UGameplayStatics::PlaySoundAtLocation(this, LoadObject<USoundBase>(nullptr, TEXT("/Game/MFImported/Sounds/CTFSFX/flagtaken.flagtaken"),
			nullptr, LOAD_NoWarn | LOAD_Quiet), Flag->HomeBase->GetActorLocation());
	}
}

void AMFGameMode_Flags::FlagDropped(AMFFlag* Flag, AMFCharacter* Was)
{
	Event(FString::Printf(TEXT("%s: mission abort! %s %s dropped!"), *NameOf(Was), TeamName(Flag->Team), *ObjectName), Flag->Team);
}

void AMFGameMode_Flags::FlagReturned(AMFFlag* Flag, AMFCharacter* By)
{
	Event(FString::Printf(TEXT("%s: %s %s has been returned to %s base!"), *NameOf(By), TeamName(Flag->Team), *ObjectName, TeamName(Flag->Team)), Flag->Team);
}

void AMFGameMode_Flags::FlagWentHome(AMFFlag* Flag)
{
}

void AMFGameMode_Flags::FlagTimedOut(AMFFlag* Flag)
{
	Event(FString::Printf(TEXT("The %s %s was returned!"), TeamName(Flag->Team), *ObjectName), Flag->Team);
	Flag->SendHome();
}

void AMFGameMode_Flags::FlagCaptured(AMFFlag* Flag, AMFCharacter* By)
{
	if (!IsMatchInProgress())
	{
		return;
	}
	if (AMFPlayerState* PS = By->GetPlayerState<AMFPlayerState>())
	{
		PS->SetScore(PS->GetScore() + 7.f);
	}
	AMFGameState* GS = GetGameState<AMFGameState>();
	if (GS && GS->TeamScores.IsValidIndex(By->GetTeam()))
	{
		GS->TeamScores[By->GetTeam()] += 1;
	}
	Event(FString::Printf(TEXT("%s: %s OBJECTIVE ACHIEVED! - %s %s %s!!!"), *NameOf(By), By->GetTeam() == 0 ? TEXT("BLUE") : TEXT("RED"),
		Flag->Team == 0 ? TEXT("BLUE") : TEXT("RED"), *ObjectName.ToUpper(), ObjectName == TEXT("gold") ? TEXT("STOLEN") : TEXT("CAPTURED")), By->GetTeam());
	Flag->SendHome();
}

// --- flag base ---------------------------------------------------------------------------

AMFFlagBase::AMFFlagBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Mesh;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cyl.Succeeded())
	{
		Mesh->SetStaticMesh(Cyl.Object);   // placeholder plinth (MF RageFlagBase: radius 60 uu)
		Mesh->SetRelativeScale3D(FVector(1.6f, 1.6f, 0.1f));
	}
}

void AMFFlagBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFFlagBase, Team);
	DOREPLIFETIME(AMFFlagBase, Flag);
}

void AMFFlagBase::BeginPlay()
{
	Super::BeginPlay();
	// MF RageFlagBase (DrawScale 1.3) stands where the legacy actor was, 130 cm above this plinth
	if (MF::ApplyPropMesh(Mesh, Team == 1 ? TEXT("RageGame/RageFlagBase1Mesh") : TEXT("RageGame/RageFlagBase0Mesh"), 1.3f, FVector(0.f, 0.f, 130.f)))
	{
		return;
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (UMaterialInterface* M = UMFGameSettings::Get().GetTeamTintMaterial())
		{
			Mesh->SetMaterial(0, M);
		}
		const FLinearColor C = UMFGameSettings::Get().GetTeamColor(Team);
		Mesh->SetCustomPrimitiveDataVector4(0, FVector4(C.R * 0.6f, C.G * 0.6f, C.B * 0.6f, 1.f));
	}
}

// --- CTF ---------------------------------------------------------------------------------

AMFGameMode_CTF::AMFGameMode_CTF()
{
	GameName = TEXT("CTF");
	GoalTeamScore = 4;      // RageCTF.GoalTeamScore
	TimeLimit = 20;
	ObjectName = TEXT("flag");
	CarryLine = TEXT("Returning to base!");
}

void AMFGameMode_CTF::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (AMFLegacyActor* L : AMFLegacyActor::FindAll(GetWorld(), TEXT("RageFlagBase")))
	{
		if (L->Team > 1)
		{
			continue;
		}
		// the legacy actor sits at the collision centre (60 uu up): base on the floor, flag above it
		AMFFlagBase* Base = GetWorld()->SpawnActor<AMFFlagBase>(AMFFlagBase::StaticClass(), L->GetActorLocation() - FVector(0, 0, 130.f), L->GetActorRotation(), SP);
		if (!Base)
		{
			continue;
		}
		Base->Team = L->Team;
		Base->Flag = SpawnFlag(L->Team, Base, Base->GetActorLocation());
		Bases.Add(Base);
	}
	if (Bases.Num() < 2)
	{
		UE_LOG(LogMF, Warning, TEXT("CTF: map has %d RageFlagBase(s), needs one per team"), Bases.Num());
	}
}

void AMFGameMode_CTF::FlagTouchedAtHome(AMFFlag* Flag, AMFCharacter* C)
{
	// MF RageBlueFlag state Home.Touch: own team carrying the enemy flag scores; enemies take it
	if (C->GetTeam() == Flag->Team)
	{
		if (AMFFlag* Carried = AMFFlag::FindCarriedBy(C); Carried && Carried != Flag)
		{
			FlagCaptured(Carried, C);
			if (const AMFGameState* GS = GetGameState<AMFGameState>(); GS && GoalTeamScore > 0 && GS->GetTeamScore(C->GetTeam()) >= GoalTeamScore)
			{
				FinishMatch(FString::Printf(TEXT("%s team wins!"), TeamName(C->GetTeam())));
			}
		}
		return;
	}
	Flag->PickUp(C);
}

void AMFGameMode_CTF::OnTimeLimit()
{
	Super::OnTimeLimit();   // most captures wins (team scores only count captures here)
}

FString AMFGameMode_CTF::GetOrderString(int32 Order) const
{
	// Rage.int OrderString 7 / 8
	if (Order == 0)
	{
		return TEXT("Guard the flag.");
	}
	if (Order == 2)
	{
		return TEXT("Capture their flag.");
	}
	return Super::GetOrderString(Order);
}

void AMFGameMode_CTF::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const
{
	const AMFPlayerState* PS = Bot ? Bot->GetPlayerState<AMFPlayerState>() : nullptr;
	const AMFCharacter* Me = Bot ? Cast<AMFCharacter>(Bot->GetPawn()) : nullptr;
	if (!PS || !Me)
	{
		return;
	}
	const uint8 MyTeam = PS->GetTeam();
	const AMFFlag* MyFlag = nullptr;
	const AMFFlag* EnemyFlag = nullptr;
	for (const AMFFlag* F : Flags)
	{
		(F->Team == MyTeam ? MyFlag : EnemyFlag) = F;
	}
	// carrying: home (wait there while our flag is away)
	if (AMFFlag::FindCarriedBy(Me))
	{
		if (MyFlag && MyFlag->HomeBase)
		{
			OutGoals.Add(MyFlag->HomeBase);
		}
		return;
	}
	// our flag away: get it back (kill the carrier / touch it)
	if (MyFlag && MyFlag->State == EMFFlagState::Dropped)
	{
		OutGoals.Add(const_cast<AMFFlag*>(MyFlag));
	}
	else if (MyFlag && MyFlag->State == EMFFlagState::Held && MyFlag->Holder)
	{
		OutGoals.Add(MyFlag->Holder);
	}
	const AMFBotController* B = Cast<AMFBotController>(Bot);
	const bool bDefend = B && (B->GetOrder() == EMFBotOrder::Defend);
	if (!bDefend && EnemyFlag)
	{
		OutGoals.Add(EnemyFlag->State == EMFFlagState::Held && EnemyFlag->Holder ? static_cast<AActor*>(EnemyFlag->Holder) : const_cast<AMFFlag*>(EnemyFlag));
	}
	if (MyFlag && MyFlag->HomeBase)
	{
		OutGoals.Add(MyFlag->HomeBase);
	}
}
