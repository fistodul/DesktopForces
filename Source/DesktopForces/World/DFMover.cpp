#include "World/DFMover.h"
#include "Game/DFGameMode.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

ADFMover::ADFMover() {
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true; // the server drops movers a game type doesn't use; doors open for all
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	RootComponent = Mesh;
	Spin = CreateDefaultSubobject<URotatingMovementComponent>(TEXT("Spin"));
	Spin->RotationRate = FRotator::ZeroRotator;
}

void ADFMover::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADFMover, Moving);
	DOREPLIFETIME(ADFMover, Alpha);
}

void ADFMover::BeginPlay() {
	Super::BeginPlay();
	BasePos = GetActorLocation();
	BaseRot = GetActorRotation();
	Spin->SetActive(!Spin->RotationRate.IsZero());
	if (HasAuthority()) {
		// GameInfo.IsRelevant
		const ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>();
		const bool bTeam = GM && GM->IsTeamGame();
		if (GM && ((!bTeamPlay && bTeam) || (!bNonTeamPlay && !bTeam) || (!CondGameType.IsNone() && CondGameType != GM->MFGameClass)))
			Destroy();
	}
}

void ADFMover::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation,
	FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) {
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	// BumpOpenTimed: a soldier walking into it; StandOpenTimed: one standing on it
	const bool bStand = InitialState == TEXT("StandOpenTimed");
	if (HasAuthority() && KeyPos.Num() > 1 && Cast<ACharacter>(Other) && (InitialState == TEXT("BumpOpenTimed") || bStand) && (!bStand || HitNormal.Z < -0.7f))
		Open();
}

void ADFMover::Open() {
	if (Moving <= 0 && Alpha < 1.f) {
		Moving = 1;
		SetActorTickEnabled(true);
	}
	GetWorldTimerManager().ClearTimer(CloseTimer);
}

void ADFMover::Close() {
	Moving = -1;
	SetActorTickEnabled(true);
}

void ADFMover::Tick(float DeltaSeconds) {
	Super::Tick(DeltaSeconds);
	if (Moving == 0 || KeyPos.Num() < 2) {
		SetActorTickEnabled(false);
		return;
	}
	Alpha = FMath::Clamp(Alpha + Moving * DeltaSeconds / FMath::Max(MoveTime, 0.01f), 0.f, 1.f);
	// UE1 movers glide smoothly between keys (InterpolateTo / PHYS_MovingBrush)
	const float S = FMath::SmoothStep(0.f, 1.f, Alpha);
	const FVector Pos = BasePos + FMath::Lerp(KeyPos[0], KeyPos[1], S);
	const FRotator Rot = BaseRot + FMath::Lerp(KeyRot.IsValidIndex(0) ? KeyRot[0] : FRotator::ZeroRotator, KeyRot.IsValidIndex(1) ? KeyRot[1] : FRotator::ZeroRotator, S);
	SetActorLocationAndRotation(Pos, Rot, true);
	if ((Moving > 0 && Alpha >= 1.f) || (Moving < 0 && Alpha <= 0.f)) {
		const bool bOpened = Moving > 0;
		Moving = 0;
		SetActorTickEnabled(false);
		if (bOpened && HasAuthority())
			GetWorldTimerManager().SetTimer(CloseTimer, this, &ADFMover::Close, StayOpenTime, false);
	}
}
