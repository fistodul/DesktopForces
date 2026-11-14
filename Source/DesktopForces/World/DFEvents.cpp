#include "World/DFEvents.h"
#include "DesktopForces.h"
#include "Game/DFGameMode.h"
#include "Player/DFCharacter.h"
#include "World/DFEffectActor.h"
#include "World/DFZoneVolume.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ADFEventActor::ADFEventActor() {
	PrimaryActorTick.bCanEverTick = false;
}

void ADFEventActor::Fire(const UObject* WorldContext, FName Name, AActor* Other, APawn* By) {
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || Name.IsNone() || World->GetNetMode() == NM_Client)
		return;
	int32 Answers = 0;
	for (TActorIterator<ADFEventActor> It(World); It; ++It)
		if (It->EventTag == Name) {
			It->Triggered(Other, By);
			++Answers;
		}
	UE_LOG(LogDF, Verbose, TEXT("Event %s from %s (by %s): %d answered"), *Name.ToString(), *GetNameSafe(WorldContext), *GetNameSafe(By), Answers);
}

void ADFEventActor::Tell(const APawn* By, const FString& Message) {
	const APlayerState* PS = By ? By->GetPlayerState() : nullptr;
	if (APlayerController* PC = PS && !Message.IsEmpty() ? Cast<APlayerController>(PS->GetOwningController()) : nullptr)
		PC->ClientMessage(Message);
}

// --- trigger ---------------------------------------------------------------------------------

ADFTrigger::ADFTrigger() {
	// UE1 actors collide as upright cylinders, wide flat ones too (a kill floor: 5000 x 100 uu),
	// which a capsule can't be
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Shape = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shape"));
	Shape->SetStaticMesh(Cylinder.Object);
	Shape->SetHiddenInGame(true);
	Shape->SetCastShadow(false);
	Shape->SetCanEverAffectNavigation(false);
	Shape->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Shape->SetCollisionObjectType(ECC_WorldDynamic);
	Shape->SetCollisionResponseToAllChannels(ECR_Ignore);
	Shape->SetGenerateOverlapEvents(true);
	RootComponent = Shape;
}

void ADFTrigger::SetCylinder(float Radius, float HalfHeight) {
	Shape->SetWorldScale3D(FVector(Radius / 50.f, Radius / 50.f, HalfHeight / 50.f));
}

void ADFTrigger::BeginPlay() {
	Super::BeginPlay();
	if (!HasAuthority()) {
		SetActorEnableCollision(false);
		return;
	}
	if (Who == EDFTriggerBy::Shot) {
		Shape->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block); // the Weapon channel
		Shape->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block); // projectiles
		return;
	}
	Shape->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	if (Who == EDFTriggerBy::Pawn || Who == EDFTriggerBy::Any)
		Shape->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Overlap);
	if (Who == EDFTriggerBy::Any) {
		Shape->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
		Shape->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	}
	Shape->OnComponentBeginOverlap.AddDynamic(this, &ADFTrigger::OnOverlap);
}

bool ADFTrigger::IsRelevant(const AActor* Other) const {
	const ADFCharacter* C = Cast<ADFCharacter>(Other);
	switch (Who) {
	case EDFTriggerBy::Player:
		return C && C->IsAlive();
	case EDFTriggerBy::Human:
		return C && C->IsAlive() && C->GetPlayerState() && !C->GetPlayerState()->IsABot();
	case EDFTriggerBy::Pawn:
		return Other && Other->IsA<APawn>();
	case EDFTriggerBy::Any:
		return Other != nullptr;
	default:
		return false;
	}
}

void ADFTrigger::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body, bool bSweep, const FHitResult& Hit) {
	Touch(Other);
}

void ADFTrigger::Touch(AActor* Other) {
	// Trigger.Touch
	const double Now = GetWorld()->GetTimeSeconds();
	if (!bActive || !IsRelevant(Other) || Now - LastFired < ReTriggerDelay)
		return;
	LastFired = Now;
	APawn* By = Cast<APawn>(Other) ? Cast<APawn>(Other) : Other->GetInstigator();
	Fire(this, Event, Other, By);
	Tell(By, Message);
	if (bOnce)
		SetActorEnableCollision(false);
	else if (RepeatTime > 0.f)
		GetWorldTimerManager().SetTimer(RepeatTimer, FTimerDelegate::CreateWeakLambda(this, [this, Still = TWeakObjectPtr<AActor>(Other)] {
			if (Still.IsValid() && Shape->IsOverlappingActor(Still.Get()))
				Touch(Still.Get());
		}),
			RepeatTime, false);
}

void ADFTrigger::Triggered(AActor* Other, APawn* By) {
	const bool bWas = bActive;
	bActive = Toggle == EDFTriggerToggle::Toggles ? !bActive : Toggle == EDFTriggerToggle::TurnsOn ? true
		: Toggle == EDFTriggerToggle::TurnsOff                                                     ? false
																								   : bActive;
	if (bActive && !bWas) {
		// CheckTouchList: whoever stands in it already
		TArray<AActor*> Inside;
		Shape->GetOverlappingActors(Inside);
		for (AActor* A : Inside)
			Touch(A);
	}
}

float ADFTrigger::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) {
	if (HasAuthority() && Who == EDFTriggerBy::Shot && bActive && Damage >= DamageThreshold && GetWorld()->GetTimeSeconds() - LastFired >= ReTriggerDelay) {
		LastFired = GetWorld()->GetTimeSeconds();
		APawn* By = EventInstigator ? EventInstigator->GetPawn() : nullptr;
		Fire(this, Event, By, By);
		Tell(By, Message);
		if (bOnce)
			SetActorEnableCollision(false);
	}
	return 0.f;
}

// --- special event, dispatcher, counter --------------------------------------------------------

void ADFEventRelay::Triggered(AActor* Other, APawn* By) {
	if (Kind == EDFEventKind::Explode) {
		// ExplodingWall.Explode: its Event, the sound (volume 2), then it's gone
		if (IsActorBeingDestroyed())
			return;
		Fire(this, Event, Other, By);
		ADFEffectActor::Spawn(GetWorld(), TEXT("WallHit"), GetActorLocation(), 2.f, false, Sound ? Sound->GetPackage()->GetName() : FString(), 1600.f, 1.f);
		Destroy();
		return;
	}
	if (Kind == EDFEventKind::Counter) {
		// Counter.Trigger: the last of NumToCount passes it on
		if (Count > 0 && --Count == 0) {
			Tell(By, Message);
			Fire(this, Event, Other, By);
		}
		return;
	}
	// SpecialEvent.Trigger: its message, to everyone or to whoever did it
	ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>();
	if (bBroadcast && GM && !Message.IsEmpty())
		GM->BroadcastChat(nullptr, Message, false);
	else
		Tell(By, Message);
	switch (Kind) {
	case EDFEventKind::Damage:
	case EDFEventKind::Kill:
		// a mapper's negative damage heals (dm-HolidayTown's apples)
		if (ADFCharacter* Healed = Kind == EDFEventKind::Damage && Damage < 0.f ? Cast<ADFCharacter>(By) : nullptr)
			Healed->Heal(FMath::RoundToInt32(-Damage));
		else if (By)
			By->TakeDamage(Kind == EDFEventKind::Kill ? 100000.f : Damage, FDamageEvent(UDFDamage_Zone::StaticClass()), nullptr, this);
		break;
	case EDFEventKind::Sound:
	case EDFEventKind::SoundForAll:
		if (Sound)
			ADFEffectActor::Spawn(GetWorld(), NAME_None, GetActorLocation(), 1.f, false, Sound->GetPackage()->GetName(), Kind == EDFEventKind::SoundForAll ? 1.e6f : 1600.f);
		break;
	case EDFEventKind::Dispatcher: {
		// Dispatcher: OutEvents in turn, each OutDelay after the one before
		float At = 0.f;
		for (const FDFOutEvent& Out : OutEvents) {
			At += FMath::Max(Out.Delay, 0.f);
			if (At <= 0.f) {
				Fire(this, Out.Event, this, By);
				continue;
			}
			FTimerHandle Timer;
			GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(this, [this, Name = Out.Event, Whose = TWeakObjectPtr<APawn>(By)] { Fire(this, Name, this, Whose.Get()); }), At, false);
		}
		break;
	}
	default:
		break;
	}
}
