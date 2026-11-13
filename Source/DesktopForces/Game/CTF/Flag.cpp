#include "Game/CTF/Flag.h"
#include "Core/DFContent.h"
#include "Core/DFProps.h"
#include "DesktopForces.h"
#include "AI/DFBotController.h"
#include "Core/DFGameSettings.h"
#include "Game/DFGameState.h"
#include "Game/DFPlayerState.h"
#include "Player/DFCharacter.h"
#include "World/DFEffectActor.h"
#include "World/DFMapActors.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace {
	AFlagGame* FlagMode(const UObject* Ctx) {
		return Ctx->GetWorld() ? Ctx->GetWorld()->GetAuthGameMode<AFlagGame>() : nullptr;
	}
} // namespace

// --- flag --------------------------------------------------------------------------------

AFlag::AFlag() {
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true; // on everyone's HUD and minimap
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(10.f);

	// MF RageBlueFlag CollisionRadius 48 uu
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(108.f);
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AFlag::OnOverlap);
	RootComponent = Trigger;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Trigger);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) {
		Mesh->SetStaticMesh(Cube.Object); // placeholder until the MF flag / gold meshes are converted
		Mesh->SetRelativeScale3D(FVector(0.08f, 0.6f, 0.4f));
		Mesh->SetRelativeLocation(FVector(0.f, 0.f, 40.f));
	}
}

void AFlag::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFlag, Team);
	DOREPLIFETIME(AFlag, State);
	DOREPLIFETIME(AFlag, Holder);
	DOREPLIFETIME(AFlag, HomeBase);
	DOREPLIFETIME(AFlag, bHiddenAtHome);
}

void AFlag::BeginPlay() {
	Super::BeginPlay();
	if (!HasAuthority())
		UpdateLook(); // here the mode says what it is first (team, gold, key), then shows it
}

AFlag* AFlag::FindCarriedBy(const ADFCharacter* C) {
	if (C) {
		for (TActorIterator<AFlag> It(C->GetWorld()); It; ++It)
			if (It->Holder == C)
				return *It;
	}
	return nullptr;
}

void AFlag::SendHome() {
	GetWorldTimerManager().ClearTimer(ReturnTimer);
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Holder = nullptr;
	State = EDFFlagState::Home;
	if (HomeBase)
		SetActorLocationAndRotation(HomeBase->GetActorLocation(), HomeBase->GetActorRotation());
	UpdateLook();
	ForceNetUpdate();
	if (AFlagGame* GM = FlagMode(this))
		GM->FlagWentHome(this);
	TouchAnyoneHere(); // someone may stand at the base already
}

void AFlag::TouchAnyoneHere() {
	TArray<AActor*> Inside;
	Trigger->GetOverlappingActors(Inside, ADFCharacter::StaticClass());
	for (AActor* A : Inside)
		OnOverlap(Trigger, A, nullptr, 0, false, FHitResult());
}

void AFlag::PickUp(ADFCharacter* By) {
	if (!By || !By->IsAlive())
		return;
	GetWorldTimerManager().ClearTimer(ReturnTimer);
	Holder = By;
	State = EDFFlagState::Held;
	AttachToActor(By, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorRelativeLocation(FVector(-30.f, 0.f, 20.f)); // on the back
	UpdateLook();
	ForceNetUpdate();
	if (AFlagGame* GM = FlagMode(this))
		GM->FlagPickedUp(this, By);
}

void AFlag::Drop() {
	ADFCharacter* Was = Holder;
	if (!Was)
		return;
	OldHolder = Was;
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Holder = nullptr;
	// MF Drop: two collision radii behind the holder, then it falls
	FVector Spot = Was->GetActorLocation() - Was->GetActorForwardVector() * 76.f;
	FHitResult Hit;
	FCollisionQueryParams Params(TEXT("Flag"), false, Was);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Was->GetActorLocation(), Spot, ECC_WorldStatic, Params))
		Spot = Was->GetActorLocation();
	if (GetWorld()->LineTraceSingleByChannel(Hit, Spot, Spot - FVector(0, 0, 5000.f), ECC_WorldStatic, Params))
		Spot = Hit.Location + FVector(0, 0, 20.f);
	if (AFlagGame* GM = FlagMode(this))
		GM->FlagDropped(this, Was);
	PlaceDropped(Spot);
}

void AFlag::PlaceDropped(const FVector& Spot, const FRotator& Rot) {
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Holder = nullptr;
	State = EDFFlagState::Dropped;
	TimeDropped = GetWorld()->GetTimeSeconds();
	SetActorLocationAndRotation(Spot, Rot);
	UpdateLook();
	ForceNetUpdate();
	// MF state Dropped: 25 s untouched and it goes home (the mode decides where)
	GetWorldTimerManager().SetTimer(ReturnTimer, FTimerDelegate::CreateWeakLambda(this, [this]() {
		if (AFlagGame* GM = FlagMode(this))
			GM->FlagTimedOut(this);
		else
			SendHome();
	}),
		25.f, false);
	TouchAnyoneHere();
}

void AFlag::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bSweep, const FHitResult& Hit) {
	ADFCharacter* C = Cast<ADFCharacter>(Other);
	AFlagGame* GM = FlagMode(this);
	// MF RageBlueFlag.Touch: a live player on foot (aPawn.VehicleIn == none)
	if (!HasAuthority() || !GM || !C || !C->IsAlive() || C->GetTeam() == DF::NoTeam || C->GetAttachParentActor())
		return;
	// A touch can be old news. Sent home off a carrier's back in the middle of that soldier's
	// move (a capture), the engine still delivers the move's overlaps afterwards: the carrier
	// "touching" the flag a map away took it again on the spot, and scored with every step into
	// the base. So whoever touches must be here, and the flag in play (the Detonation key in its
	// lock is not)
	if (!GetActorEnableCollision() || FVector::Dist(C->GetActorLocation(), GetActorLocation()) > Trigger->GetScaledSphereRadius() + C->GetSimpleCollisionHalfHeight() + C->GetSimpleCollisionRadius())
		return;
	if (State == EDFFlagState::Home) {
		GM->FlagTouchedAtHome(this, C);
		return;
	}
	if (State != EDFFlagState::Dropped || (C == OldHolder.Get() && GetWorld()->GetTimeSeconds() < TimeDropped + 1.f))
		return;
	if (C->GetTeam() == Team) {
		GM->FlagReturned(this, C);
		SendHome();
		return;
	}
	PickUp(C);
}

void AFlag::OnRep_State() {
	UpdateLook();
}

void AFlag::UpdateLook() {
	SetActorHiddenInGame(State == EDFFlagState::Home && bHiddenAtHome);
	if (GetNetMode() == NM_DedicatedServer)
		return;
	// MF models: the detonation key (RageDetonationKey), Safecracker's gold (RageSafeObject)
	const TCHAR* Prop = Team == DF::NoTeam ? TEXT("RageGame/RageDetKeyMesh")
		: bHiddenAtHome                    ? (Team == 1 ? TEXT("RageGame/SafeObjRedT") : TEXT("RageGame/SafeObjBlueT"))
										   : nullptr;
	Mesh->SetVisibility(Prop != nullptr);
	if (Staff)
		Staff->SetVisibility(!Prop, true);
	if (Prop && DF::ApplyPropMesh(Mesh, Prop, 1.f, FVector(0.f, 0.f, 30.f)))
		return;
	if (Prop) {
		DF::Tint(Mesh, UDFGameSettings::Get().GetTeamColor(Team)); // the key or the gold without their models: a tinted block
		return;
	}
	// The CTF flag: MF's RageGame.CTFFlagMesh, a staff with the team's emblem waving from it;
	// without the classic pack a 2.2 m staff and a flat cloth
	if (!Staff) {
		Staff = NewObject<USceneComponent>(this);
		Staff->SetupAttachment(Trigger);
		Staff->RegisterComponent();
		Cloth = DF::MakeFlag(Staff, TEXT("CTFFlagMesh"), TEXT("Skin1"));
	}
	if (!Cloth) {
		UStaticMeshComponent* Pole = NewObject<UStaticMeshComponent>(this);
		Pole->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
		Pole->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Pole->SetCastShadow(false);
		Pole->SetCanEverAffectNavigation(false);
		Pole->SetupAttachment(Staff);
		Pole->RegisterComponent();
		Pole->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
		Pole->SetRelativeScale3D(FVector(0.05f, 0.05f, 2.2f));
		DF::Tint(Pole, FLinearColor(0.22f, 0.2f, 0.17f)); // CTFFlagTex2: dull metal
		Cloth = DF::MakeFlatFlag(Staff, FVector(-2.f, 0.f, 186.f), 90.f, 60.f);
	}
	DF::SetFlagEmblem(Cloth, Team);
	// MF state Held: DrawScale 0.6 and rolled 4000 (22 degrees), over the holder's shoulder
	const bool bHeld = State == EDFFlagState::Held;
	// MF FlagOffset (-10, 0, 30 uu): its mesh, centred on the staff, stands that far off the base
	const FVector AtHome = State == EDFFlagState::Home && Cloth->IsA<USkeletalMeshComponent>() ? FVector(-22.5f, 0.f, 67.5f) : FVector::ZeroVector;
	Staff->SetRelativeLocationAndRotation(bHeld ? FVector(0.f, 0.f, -45.f) : AtHome, FRotator(bHeld ? 22.f : 0.f, 0.f, 0.f));
	Staff->SetRelativeScale3D(FVector(bHeld ? 0.6f : 1.f));
}

// --- flag modes --------------------------------------------------------------------------

AFlagGame::AFlagGame() {
	ScoreLimit = 0;
	NumTeams = 2;
	RespawnWait = 5;
	bScoreTeamKills = false;
}

FString AFlagGame::NameOf(const APawn* P) {
	return P && P->GetPlayerState() ? P->GetPlayerState()->GetPlayerName() : FString(TEXT("Someone"));
}

AFlag* AFlagGame::SpawnFlag(uint8 Team, AActor* Home, const FVector& Location) {
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFlag* F = GetWorld()->SpawnActor<AFlag>(AFlag::StaticClass(), FTransform(Location), SP);
	if (F) {
		F->Team = Team;
		F->HomeBase = Home;
		F->SendHome();
		Flags.Add(F);
	}
	return F;
}

void AFlagGame::Killed(AController* Killer, AController* Victim, ADFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType) {
	// MF RageCTF / RageSafeCracker.Killed: the carrier drops it; killing them is worth 4
	if (AFlag* F = AFlag::FindCarriedBy(VictimPawn)) {
		ADFPlayerState* KPS = Killer ? Killer->GetPlayerState<ADFPlayerState>() : nullptr;
		const ADFPlayerState* VPS = Victim ? Victim->GetPlayerState<ADFPlayerState>() : nullptr;
		if (KPS && VPS && KPS != VPS && KPS->GetTeam() != VPS->GetTeam())
			KPS->SetScore(KPS->GetScore() + 4.f);
		F->Drop();
	}
	Super::Killed(Killer, Victim, VictimPawn, DamageType);
}

void AFlagGame::CharacterRemoved(ADFCharacter* Character) {
	if (AFlag* F = AFlag::FindCarriedBy(Character))
		F->Drop();
}

void AFlagGame::FlagPickedUp(AFlag* Flag, ADFCharacter* By) {
	// RageCTFMessage / RageSCMessage 6 (pickup): TakenSound
	Event(FString::Printf(TEXT("%s: I have the %s %s! %s"), *NameOf(By), *TeamName(Flag->Team), *ObjectName, *CarryLine), Flag->Team,
		ObjectName == TEXT("gold") ? TEXT("Gold_captured") : TEXT("Flag_captured"));
	if (Flag->HomeBase)
		ADFEffectActor::SpawnSound(GetWorld(), TEXT("CTFSFX.flagtaken"), Flag->HomeBase->GetActorLocation());
}

void AFlagGame::FlagDropped(AFlag* Flag, ADFCharacter* Was) {
	Event(FString::Printf(TEXT("%s: mission abort! %s %s dropped!"), *NameOf(Was), *TeamName(Flag->Team), *ObjectName), Flag->Team);
}

void AFlagGame::FlagReturned(AFlag* Flag, ADFCharacter* By) {
	Event(FString::Printf(TEXT("%s: %s %s has been returned to %s base!"), *NameOf(By), *TeamName(Flag->Team), *ObjectName, *TeamName(Flag->Team)), Flag->Team,
		ObjectName == TEXT("gold") ? TEXT("Gold_returned") : TEXT("Flag_returned"));
}

void AFlagGame::FlagWentHome(AFlag* Flag) {
}

void AFlagGame::FlagTimedOut(AFlag* Flag) {
	Event(FString::Printf(TEXT("The %s %s was returned!"), *TeamName(Flag->Team), *ObjectName), Flag->Team,
		ObjectName == TEXT("gold") ? TEXT("Gold_returned") : TEXT("Flag_returned"));
	Flag->SendHome();
}

void AFlagGame::FlagCaptured(AFlag* Flag, ADFCharacter* By) {
	if (!IsMatchInProgress())
		return;
	if (ADFPlayerState* PS = By->GetPlayerState<ADFPlayerState>())
		PS->SetScore(PS->GetScore() + 7.f);
	ADFGameState* GS = GetGameState<ADFGameState>();
	if (GS && GS->TeamScores.IsValidIndex(By->GetTeam()))
		GS->TeamScores[By->GetTeam()] += 1;
	Event(FString::Printf(TEXT("%s: %s OBJECTIVE ACHIEVED! - %s %s %s!!!"), *NameOf(By), *TeamName(By->GetTeam()).ToUpper(),
			  *TeamName(Flag->Team).ToUpper(), *ObjectName.ToUpper(), ObjectName == TEXT("gold") ? TEXT("STOLEN") : TEXT("CAPTURED")),
		By->GetTeam(),
		ObjectName == TEXT("gold") ? NAME_None : FName(By->GetTeam() == 0 ? TEXT("Blue_score") : TEXT("Red_score"))); // RageCTFMessage CapturedSound
	Flag->SendHome();
}

// --- flag base ---------------------------------------------------------------------------

AFlagBase::AFlagBase() {
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	// not the mesh: ApplyPropMesh places it relative to this, the map's spot
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cyl.Succeeded()) {
		Mesh->SetStaticMesh(Cyl.Object); // placeholder plinth (MF RageFlagBase: radius 60 uu)
		Mesh->SetRelativeScale3D(FVector(1.6f, 1.6f, 0.1f));
	}
}

void AFlagBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFlagBase, Team);
	DOREPLIFETIME(AFlagBase, Flag);
}

void AFlagBase::BeginPlay() {
	Super::BeginPlay();
	// MF RageFlagBase (DrawScale 1.3) stands where the legacy actor was, 130 cm above this plinth
	if (DF::ApplyPropMesh(Mesh, Team == 1 ? TEXT("RageGame/RageFlagBase1Mesh") : TEXT("RageGame/RageFlagBase0Mesh"), 1.3f, FVector(0.f, 0.f, 130.f)))
		return;
	DF::Tint(Mesh, UDFGameSettings::Get().GetTeamColor(Team) * 0.6f);
}

// --- CTF ---------------------------------------------------------------------------------

ACaptureTheFlag::ACaptureTheFlag() {
	GameName = TEXT("CTF");
	MFGameClass = TEXT("RageCTF");
	InstructionSound = TEXT("Get_the_flag");
	ScoreLimit = 4; // RageCTF: captures to win
	TimeLimit = 20;
	ObjectName = TEXT("flag");
	CarryLine = TEXT("Returning to base!");
}

void ACaptureTheFlag::HandleMatchHasStarted() {
	Super::HandleMatchHasStarted();
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (ADFLegacyActor* L : ADFLegacyActor::FindAll(GetWorld(), TEXT("RageFlagBase"))) {
		if (L->Team > 1)
			continue;
		// the legacy actor sits at the collision centre (60 uu up): base on the floor, flag above it
		AFlagBase* Base = GetWorld()->SpawnActor<AFlagBase>(AFlagBase::StaticClass(), L->GetActorLocation() - FVector(0, 0, 130.f), L->GetActorRotation(), SP);
		if (!Base)
			continue;
		Base->Team = L->Team;
		Base->Flag = SpawnFlag(L->Team, Base, Base->GetActorLocation());
		Bases.Add(Base);
	}
	if (Bases.Num() < 2)
		UE_LOG(LogDF, Warning, TEXT("CTF: map has %d RageFlagBase(s), needs one per team"), Bases.Num());
}

void ACaptureTheFlag::FlagTouchedAtHome(AFlag* Flag, ADFCharacter* C) {
	// MF RageBlueFlag state Home.Touch: own team carrying the enemy flag scores; enemies take it
	if (C->GetTeam() == Flag->Team) {
		if (AFlag* Carried = AFlag::FindCarriedBy(C); Carried && Carried != Flag) {
			FlagCaptured(Carried, C);
			if (const ADFGameState* GS = GetGameState<ADFGameState>(); GS && ScoreLimit > 0 && GS->GetTeamScore(C->GetTeam()) >= ScoreLimit)
				FinishMatch(FString::Printf(TEXT("%s team wins!"), *TeamName(C->GetTeam())));
		}
		return;
	}
	Flag->PickUp(C);
}

void ACaptureTheFlag::OnTimeLimit() {
	Super::OnTimeLimit(); // most captures wins (team scores only count captures here)
}

FString ACaptureTheFlag::GetOrderString(int32 Order) const {
	// Rage.int OrderString 7 / 8
	if (Order == 0)
		return TEXT("Guard the flag.");
	if (Order == 2)
		return TEXT("Capture their flag.");
	return Super::GetOrderString(Order);
}

void ACaptureTheFlag::GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const {
	const ADFPlayerState* PS = Bot ? Bot->GetPlayerState<ADFPlayerState>() : nullptr;
	const ADFCharacter* Me = Bot ? Cast<ADFCharacter>(Bot->GetPawn()) : nullptr;
	if (!PS || !Me)
		return;
	const uint8 MyTeam = PS->GetTeam();
	const AFlag* MyFlag = nullptr;
	const AFlag* EnemyFlag = nullptr;
	for (const AFlag* F : Flags)
		(F->Team == MyTeam ? MyFlag : EnemyFlag) = F;
	// carrying: home (wait there while our flag is away)
	if (AFlag::FindCarriedBy(Me)) {
		if (MyFlag && MyFlag->HomeBase)
			OutGoals.Add(MyFlag->HomeBase);
		return;
	}
	// our flag away: get it back (kill the carrier / touch it)
	if (MyFlag && MyFlag->State == EDFFlagState::Dropped)
		OutGoals.Add(const_cast<AFlag*>(MyFlag));
	else if (MyFlag && MyFlag->State == EDFFlagState::Held && MyFlag->Holder)
		OutGoals.Add(MyFlag->Holder);
	const ADFBotController* B = Cast<ADFBotController>(Bot);
	const bool bDefend = B && (B->GetOrder() == EDFBotOrder::Defend);
	if (!bDefend && EnemyFlag)
		OutGoals.Add(EnemyFlag->State == EDFFlagState::Held && EnemyFlag->Holder ? static_cast<AActor*>(EnemyFlag->Holder) : const_cast<AFlag*>(EnemyFlag));
	if (MyFlag && MyFlag->HomeBase)
		OutGoals.Add(MyFlag->HomeBase);
}

FName ACaptureTheFlag::GetOrderSound(int32 Order) const {
	// RageVoicePack.SetOrderMessage: OrderSound 7 / 8
	return Order == 0 ? FName(TEXT("Guard_the_flag")) : Order == 2 ? FName(TEXT("Capture_their_flag"))
																   : Super::GetOrderSound(Order);
}
