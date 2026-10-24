#include "Game/Safecracker/Safecracker.h"
#include "Vehicles/VehicleFrag.h"
#include "Core/DFContent.h"
#include "Core/DFProps.h"
#include "DesktopForces.h"
#include "AI/DFBotController.h"
#include "Core/DFGameSettings.h"
#include "Core/DFTypes.h"
#include "Game/DFGameState.h"
#include "Game/DFPlayerState.h"
#include "Player/DFCharacter.h"
#include "Sound/SoundBase.h"
#include "World/DFMapActors.h"
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

namespace {
	ASafecracker* SCMode(const UObject* Ctx) {
		return Ctx->GetWorld() ? Ctx->GetWorld()->GetAuthGameMode<ASafecracker>() : nullptr;
	}
} // namespace

// --- safe ------------------------------------------------------------------------------

ASafe::ASafe() {
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true; // on everyone's HUD (safe meter) and minimap
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
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ASafe::OnOverlap);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Body);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) {
		Mesh->SetStaticMesh(Cube.Object);
		Mesh->SetRelativeScale3D(FVector(1.8f)); // placeholder until the MF safe mesh is converted
	}
}

void ASafe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASafe, Team);
	DOREPLIFETIME(ASafe, Health);
	DOREPLIFETIME(ASafe, bOpen);
	DOREPLIFETIME(ASafe, bCrackable);
	DOREPLIFETIME(ASafe, Gold);
}

void ASafe::BeginPlay() {
	Super::BeginPlay();
	if (GetNetMode() != NM_DedicatedServer) {
		if (UMaterialInterface* M = UDFGameSettings::Get().GetTeamTintMaterial())
			Mesh->SetMaterial(0, M);
		UpdateLook();
	}
}

void ASafe::Init(uint8 InTeam, bool bInCrackable) {
	Team = InTeam;
	bCrackable = bInCrackable;
	bOpen = true; // MF PostBeginPlay: bOpen = true; CloseSafe()
	CloseSafe();
	UpdateLook();
}

float ASafe::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) {
	// MF RageSafe.TakeDamage: only the defending team's safe, only enemies, scaled down hard
	if (!HasAuthority() || !bCrackable || bOpen || Health < 0.f)
		return 0.f;
	const ADFPlayerState* PS = EventInstigator ? EventInstigator->GetPlayerState<ADFPlayerState>() : nullptr;
	if (PS && PS->GetTeam() == Team)
		return 0.f;
	const UDFDamageType* Type = Cast<UDFDamageType>(DamageEvent.DamageTypeClass ? DamageEvent.DamageTypeClass->GetDefaultObject() : nullptr);
	const float Actual = FMath::Max(Damage * ((Type && Type->bExplosive) ? 0.15f : 0.05f), 1.f);
	Health -= Actual;
	if (Health < 0.f) {
		if (const ASafecracker* GM = SCMode(this); GM && GM->AutoCloseTime > 0) {
			GetWorldTimerManager().SetTimer(CloseTimer, FTimerDelegate::CreateWeakLambda(this, [this]() {
				if (CloseSafe()) {
					if (ASafecracker* M = SCMode(this))
						M->Event(FString::Printf(TEXT("The %s safe is repaired!"), AFlagGame::TeamName(Team)), Team);
					GetWorldTimerManager().ClearTimer(CloseTimer);
				}
			}),
				float(GM->AutoCloseTime), true);
		}
		OpenSafe();
		MulticastCrackFX();
	}
	ForceNetUpdate();
	return Actual;
}

void ASafe::OpenSafe() {
	if (bOpen || !Gold || Gold->State != EDFFlagState::Home)
		return;
	bOpen = true;
	Gold->bHiddenAtHome = false;
	Gold->UpdateLook();
	UpdateLook();
	if (ASafecracker* GM = SCMode(this))
		GM->Event(FString::Printf(TEXT("The %s safe has been blown!"), AFlagGame::TeamName(Team)), Team);
	// someone may already stand at it
	TArray<AActor*> Inside;
	Trigger->GetOverlappingActors(Inside, ADFCharacter::StaticClass());
	for (AActor* A : Inside)
		TouchedBy(Cast<ADFCharacter>(A));
}

bool ASafe::CloseSafe() {
	if (!bOpen || !Gold || Gold->State != EDFFlagState::Home)
		return false;
	bOpen = false;
	Health = 100.f;
	Gold->bHiddenAtHome = true; // inside the shut safe
	Gold->UpdateLook();
	UpdateLook();
	return true;
}

void ASafe::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bSweep, const FHitResult& Hit) {
	TouchedBy(Cast<ADFCharacter>(Other));
}

void ASafe::TouchedBy(ADFCharacter* C) {
	ASafecracker* GM = SCMode(this);
	if (!HasAuthority() || !GM || !C || !C->IsAlive() || !Gold)
		return;
	if (C->GetTeam() == Team) {
		// MF RageSafe.Touch: own safe + carrying the enemy gold = score
		if (AFlag* Carried = AFlag::FindCarriedBy(C); Carried && Carried != Gold)
			GM->FlagCaptured(Carried, C);
	} else if (bOpen && Gold->State == EDFFlagState::Home) {
		Gold->PickUp(C);
	}
}

void ASafe::MulticastCrackFX_Implementation() {
	// RageSafe.TakeDamage: AddVehicleExplosionServer(Size 2)
	if (GetNetMode() != NM_DedicatedServer) {
		DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/MiscSFX/Explode")), GetActorLocation(), 1.f, 2000.f);
		AVehicleFrag::Explosion(GetWorld(), GetActorLocation(), 0.8f);
	}
}

void ASafe::OnRep_Open() {
	UpdateLook();
}

void ASafe::UpdateLook() {
	// MF RageSafe meshes per team, the blown-open one once cracked
	const TCHAR* Prop = bOpen ? (Team == 1 ? TEXT("RageGame/RageSafeOpenMeshRed") : TEXT("RageGame/RageSafeOpenMeshBlue"))
							  : (Team == 1 ? TEXT("RageGame/RageSafeMeshRed") : TEXT("RageGame/RageSafeMeshBlue"));
	if (DF::ApplyPropMesh(Mesh, Prop))
		return;
	FLinearColor C = UDFGameSettings::Get().GetTeamColor(Team);
	if (bOpen)
		C *= 0.35f; // blown: dark (the MF open-safe mesh until it's converted)
	Mesh->SetCustomPrimitiveDataVector4(0, FVector4(C.R, C.G, C.B, 1.f));
}

// --- game mode -------------------------------------------------------------------------

ASafecracker::ASafecracker() {
	GameName = TEXT("SafeCracker");
	MFGameClass = TEXT("RageSafeCracker");
	TimeLimit = 9; // RageSafeCracker.TimeLimit
	GoalTeamScore = 1;
	RespawnWait = 5; // RespawnWait / MissionRespawnWait
	ObjectName = TEXT("gold");
	CarryLine = TEXT("Transport in progress!");
}

void ASafecracker::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) {
	Super::InitGame(MapName, Options, ErrorMessage);
	Part = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Part"), 1), 1, 2);
	FirstCaptureTime = UGameplayStatics::GetIntOption(Options, TEXT("FirstCaptureTime"), 0);
	if (TimeLimit < 1)
		TimeLimit = 8; // RageSafeCracker.InitGame
	if (Part == 2 && FirstCaptureTime > 0)
		TimeLimitSeconds = FirstCaptureTime; // beat part 1's time
}

FName ASafecracker::GetInstructionSound(const AController* C) const {
	const ADFPlayerState* PS = C ? C->GetPlayerState<ADFPlayerState>() : nullptr;
	return PS && PS->GetTeam() == AttackerTeam() ? TEXT("Crack_their_Safe") : TEXT("Defend_your_Safe");
}

void ASafecracker::HandleMatchHasStarted() {
	Super::HandleMatchHasStarted();
	SpawnSafes();
	Event(FString::Printf(TEXT("Part %d: %s team attacks, %s team defends its gold."), Part,
			  TeamName(AttackerTeam()), TeamName(DefenderTeam())),
		255);
}

void ASafecracker::SpawnSafes() {
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (ADFLegacyActor* L : ADFLegacyActor::FindAll(GetWorld(), TEXT("RageSafe"))) {
		if (L->Team > 1)
			continue;
		ASafe* Safe = GetWorld()->SpawnActor<ASafe>(ASafe::StaticClass(), L->GetActorTransform(), SP);
		if (!Safe)
			continue;
		Safe->Gold = SpawnFlag(L->Team, Safe, Safe->GetActorLocation());
		if (Safe->Gold)
			Safe->Gold->bHiddenAtHome = true;
		Safe->Init(L->Team, L->Team == DefenderTeam());
		Safes.Add(Safe);
	}
	if (Safes.Num() < 2)
		UE_LOG(LogDF, Warning, TEXT("Safecracker: map has %d RageSafe(s), needs one per team"), Safes.Num());
}

void ASafecracker::FlagTouchedAtHome(AFlag* Flag, ADFCharacter* C) {
	// the gold at home is inside its safe: taken only once the safe is blown
	for (ASafe* S : Safes)
		if (S->Gold == Flag)
			S->TouchedBy(C);
}

void ASafecracker::FlagCaptured(AFlag* Flag, ADFCharacter* By) {
	if (!IsMatchInProgress())
		return;
	Super::FlagCaptured(Flag, By); // +7, team point, the line, gold home
	const ADFGameState* GS = GetGameState<ADFGameState>();
	const int32 Used = GS ? (GS->TimeLimitSeconds > 0 ? GS->TimeLimitSeconds : GS->TimeLimit * 60) - GS->GetRemainingTime() : 0;
	if (Part == 1)
		FirstCaptureTime = Used;
	FinishMatch(FString::Printf(TEXT("%s stole the gold in %d:%02d%s"), TeamName(By->GetTeam()), Used / 60, Used % 60,
		Part == 2 ? TEXT(" and win!") : TEXT("")));
}

void ASafecracker::OnTimeLimit() {
	// MF SetEndCams: nobody delivered the gold in time
	if (Part == 1)
		FirstCaptureTime = 0;
	if (ADFGameState* GS = GetGameState<ADFGameState>(); GS && GS->TeamScores.IsValidIndex(DefenderTeam()))
		GS->TeamScores[DefenderTeam()] += 1;
	FinishMatch(FString::Printf(TEXT("%s safeguarded their gold%s"), TeamName(DefenderTeam()), Part == 2 ? TEXT(" and win!") : TEXT("!")));
}

namespace SafecrackerLocal {
	/** The URL options without part 2's (Part, FirstCaptureTime). */
	FString WithoutPart(FString Opts) {
		for (const TCHAR* Key : { TEXT("Part"), TEXT("FirstCaptureTime") }) {
			const FString V = UGameplayStatics::ParseOption(Opts, Key);
			Opts = Opts.Replace(*FString::Printf(TEXT("?%s=%s"), Key, *V), TEXT(""));
		}
		return Opts;
	}
} // namespace SafecrackerLocal

void ASafecracker::RestartGame() {
	// a restart (vote) starts over at part 1
	GetWorld()->ServerTravel(GetWorld()->GetOutermost()->GetName() + SafecrackerLocal::WithoutPart(OptionsString), false);
}

void ASafecracker::TravelAfterMatch() {
	if (Part == 1) {
		// MF RestartGame: part 2 on the same map, sides swapped, part 1's time to beat
		GetWorld()->ServerTravel(FString::Printf(TEXT("%s%s?Part=2?FirstCaptureTime=%d"), *GetWorld()->GetOutermost()->GetName(),
									 *SafecrackerLocal::WithoutPart(OptionsString), FirstCaptureTime),
			false);
		return;
	}
	Super::TravelAfterMatch(); // the rotation's next map, else part 1 again
}

FString ASafecracker::GetOrderString(int32 Order) const {
	// Rage.int OrderString 11 / 12
	if (Order == 0)
		return TEXT("Guard the safe.");
	if (Order == 2)
		return TEXT("Grab the gold.");
	return Super::GetOrderString(Order);
}

void ASafecracker::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const {
	const ADFPlayerState* PS = Bot ? Bot->GetPlayerState<ADFPlayerState>() : nullptr;
	const ADFCharacter* Me = Bot ? Cast<ADFCharacter>(Bot->GetPawn()) : nullptr;
	if (!PS || !Me)
		return;
	const uint8 MyTeam = PS->GetTeam();
	ASafe* MySafe = nullptr;
	ASafe* EnemySafe = nullptr;
	for (ASafe* S : Safes)
		(S->Team == MyTeam ? MySafe : EnemySafe) = S;
	// carrying the gold: home (MF PriorityObjective 255 near home)
	if (AFlag::FindCarriedBy(Me)) {
		if (MySafe)
			OutGoals.Add(MySafe);
		return;
	}
	// dropped gold: grab the enemy's, return ours
	for (AFlag* F : Flags)
		if (F->State == EDFFlagState::Dropped)
			OutGoals.Add(F);
	const ADFBotController* B = Cast<ADFBotController>(Bot);
	const EDFBotOrder Order = B ? B->GetOrder() : EDFBotOrder::Freelance;
	if (MyTeam == AttackerTeam() && Order != EDFBotOrder::Defend) {
		if (EnemySafe)
			OutGoals.Add(EnemySafe); // crack it, then take the gold
	} else if (MySafe) {
		// defenders (and attackers told to defend): chase our gold's carrier, else guard the safe
		for (AFlag* F : Flags)
			if (F->Team == MyTeam && F->State == EDFFlagState::Held && F->Holder)
				OutGoals.Add(F->Holder);
		OutGoals.Add(MySafe);
	}
}

AActor* ASafecracker::GetBotShootTarget(AController* Bot) const {
	// MF RageSafePawn: bots shoot the enemy's closed, crackable safe when they see it
	const ADFPlayerState* PS = Bot ? Bot->GetPlayerState<ADFPlayerState>() : nullptr;
	const APawn* Me = Bot ? Bot->GetPawn() : nullptr;
	if (!PS || !Me || PS->GetTeam() != AttackerTeam())
		return nullptr;
	for (ASafe* S : Safes)
		if (S->bCrackable && !S->bOpen && S->Team != PS->GetTeam() && FVector::Dist(S->GetActorLocation(), Me->GetActorLocation()) < 2000.f && Bot->LineOfSightTo(S))
			return S;
	return nullptr;
}

FName ASafecracker::GetOrderSound(int32 Order) const {
	// RageVoicePack.SetOrderMessage: OrderSound 11 / 12
	return Order == 0 ? FName(TEXT("Guard_the_safe")) : Order == 2 ? FName(TEXT("Grab_the_gold"))
																   : Super::GetOrderSound(Order);
}
