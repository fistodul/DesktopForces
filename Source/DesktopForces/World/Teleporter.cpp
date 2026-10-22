#include "World/Teleporter.h"
#include "DesktopForces.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "TimerManager.h"
#include "NavigationSystemTypes.h"
#include "AI/NavigationModifier.h"
#include "NavigationOctree.h"

ATeleporter::ATeleporter()
{
	PrimaryActorTick.bCanEverTick = false;
	Trigger = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Trigger"));
	Trigger->InitCapsuleSize(40.5f, 90.f);                       // Teleporter CollisionRadius 18, CollisionHeight 40
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->SetCanEverAffectNavigation(false);
	RootComponent = Trigger;
}

void ATeleporter::BeginPlay()
{
	Super::BeginPlay();
	if (URL.IsEmpty())
	{
		Trigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // destination only
	}
	else if (HasAuthority())
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &ATeleporter::OnTouch);
		Trigger->OnComponentEndOverlap.AddDynamic(this, &ATeleporter::OnUntouch);
	}
}

void ATeleporter::OnUntouch(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex)
{
	Arrived.Remove(Other);
}

void ATeleporter::OnTouch(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	// soldiers only (bCanTeleport): vehicles and pickups stay
	APawn* P = Cast<ACharacter>(Other);
	if (!bEnabled || !P || Arrived.Contains(Other) || P->GetAttachParentActor())
	{
		return;
	}
	// a random teleporter Tagged URL, not this one
	TArray<ATeleporter*> Dests;
	for (TActorIterator<ATeleporter> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->TeleporterTag.ToString().Equals(URL, ESearchCase::IgnoreCase))
		{
			Dests.Add(*It);
		}
	}
	if (Dests.Num() == 0)
	{
		UE_LOG(LogDF, Warning, TEXT("Teleport destination for %s (%s) not found"), *GetName(), *URL);
		return;
	}
	Dests[FMath::RandRange(0, Dests.Num() - 1)]->Accept(P, this);
}

bool ATeleporter::Accept(APawn* Incoming, const ATeleporter* Source)
{
	const FRotator OldRot = Incoming->GetControlRotation();
	FRotator NewRot = OldRot;
	if (bChangesYaw)
	{
		NewRot.Yaw = GetActorRotation().Yaw + (Source ? 180.f + OldRot.Yaw - Source->GetActorRotation().Yaw : 0.f);
	}
	Arrived.Add(Incoming);
	if (!Incoming->TeleportTo(GetActorLocation(), FRotator(0.f, NewRot.Yaw, 0.f)))
	{
		Arrived.Remove(Incoming);
		return false;
	}
	if (AController* C = Incoming->GetController())
	{
		C->SetControlRotation(NewRot);
	}
	if (UCharacterMovementComponent* Move = Cast<ACharacter>(Incoming) ? Cast<ACharacter>(Incoming)->GetCharacterMovement() : nullptr)
	{
		FVector V = Move->Velocity;
		if (bChangesVelocity)
		{
			V = TargetVelocity;
		}
		else
		{
			if (bChangesYaw)
			{
				// the speed along the old view now goes along the new one
				const FVector OldDir = FRotator(Move->IsMovingOnGround() ? 0.f : OldRot.Pitch, OldRot.Yaw, 0.f).Vector();
				const float Mag = FVector::DotProduct(V, OldDir);
				V = V - Mag * OldDir + Mag * FRotator(0.f, NewRot.Yaw, 0.f).Vector();
			}
			V.X *= bReversesX ? -1.f : 1.f;
			V.Y *= bReversesY ? -1.f : 1.f;
			V.Z *= bReversesZ ? -1.f : 1.f;
		}
		Move->Velocity = V;
	}
	return true;
}

TArray<FNavigationLink> ATeleporter::MakeLinks() const
{
	TArray<FNavigationLink> Links;
	const FVector Foot(0.f, 0.f, -Trigger->GetUnscaledCapsuleHalfHeight());
	for (const FVector& T : LinkTargets)
	{
		FNavigationLink L(Foot, T);
		L.Direction = ENavLinkDirection::LeftToRight;
		L.SnapRadius = 60.f;
		Links.Add(L);
	}
	return Links;
}

void ATeleporter::GetNavigationData(FNavigationRelevantData& Data) const
{
	NavigationHelper::ProcessNavLinkAndAppend(&Data.Modifiers, this, MakeLinks());
}

FBox ATeleporter::GetNavigationBounds() const
{
	FBox B(GetActorLocation() - FVector(100.f), GetActorLocation() + FVector(100.f));
	for (const FVector& T : LinkTargets)
	{
		B += GetActorTransform().TransformPosition(T);
	}
	return B.ExpandBy(60.f);
}

bool ATeleporter::GetNavigationLinksArray(TArray<FNavigationLink>& OutLink, TArray<FNavigationSegmentLink>& OutSegments) const
{
	OutLink.Append(MakeLinks());
	return LinkTargets.Num() > 0;
}

// --- periodic sound ------------------------------------------------------------------------

ADFPeriodicSound::ADFPeriodicSound()
{
	PrimaryActorTick.bCanEverTick = false;
	Audio = CreateDefaultSubobject<UAudioComponent>(TEXT("Audio"));
	Audio->bAutoActivate = false;
	RootComponent = Audio;
}

void ADFPeriodicSound::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer || !Audio->Sound)
	{
		return;
	}
	if (Period <= 0.f)
	{
		Audio->Play();   // no Period: the plain ambient loop
		return;
	}
	Switch();
}

void ADFPeriodicSound::Switch()
{
	// Progress runs PhaseShift .. Period + Interval: on while Progress < Period
	const float Cycle = Period + FMath::Max(Interval, 0.f);
	const float P = FMath::Fmod(GetWorld()->GetTimeSeconds() + PhaseShift, Cycle);
	const bool bOn = P < Period;
	if (bOn != Audio->IsPlaying())
	{
		bOn ? Audio->Play() : Audio->Stop();
	}
	GetWorldTimerManager().SetTimer(SwitchTimer, this, &ADFPeriodicSound::Switch, FMath::Max((bOn ? Period : Cycle) - P, 0.05f), false);
}
