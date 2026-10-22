#include "World/DFLadder.h"
#include "Player/DFCharacter.h"
#include "Player/DFCharacterMovement.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "NavigationSystemTypes.h"
#include "AI/NavigationModifier.h"
#include "NavigationOctree.h"

ADFLadderVolume::ADFLadderVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	Volume = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Volume"));
	Volume->SetCollisionProfileName(TEXT("Trigger"));
	Volume->SetGenerateOverlapEvents(true);
	Volume->SetMobility(EComponentMobility::Static);
	Volume->bHiddenInGame = true;
	RootComponent = Volume;
}

void ADFLadderVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Volume->SetCapsuleSize(Radius, HalfHeight);
}

void ADFLadderVolume::BeginPlay()
{
	Super::BeginPlay();
	Volume->SetCapsuleSize(Radius, HalfHeight);
	Volume->OnComponentBeginOverlap.AddDynamic(this, &ADFLadderVolume::OnBegin);
	Volume->OnComponentEndOverlap.AddDynamic(this, &ADFLadderVolume::OnEnd);
}

FNavigationLink ADFLadderVolume::MakeLink() const
{
	FNavigationLink Link(LinkBottom, LinkTop);
	Link.Direction = ENavLinkDirection::BothWays;
	Link.SnapRadius = 60.f;
	return Link;
}

void ADFLadderVolume::GetNavigationData(FNavigationRelevantData& Data) const
{
	NavigationHelper::ProcessNavLinkAndAppend(&Data.Modifiers, this, { MakeLink() });
}

FBox ADFLadderVolume::GetNavigationBounds() const
{
	const FTransform& T = GetActorTransform();
	return FBox(TArray<FVector>{ T.TransformPosition(LinkBottom), T.TransformPosition(LinkTop) }).ExpandBy(60.f);
}

bool ADFLadderVolume::GetNavigationLinksArray(TArray<FNavigationLink>& OutLink, TArray<FNavigationSegmentLink>& OutSegments) const
{
	OutLink.Add(MakeLink());
	return true;
}

#if WITH_EDITOR
void ADFLadderVolume::PlaceNavLinks()
{
	UWorld* World = GetWorld();
	const FVector Loc = GetActorLocation();
	const FVector Fwd = GetActorForwardVector().GetSafeNormal2D();   // towards the wall
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFLadderLinks), true, this);
	FHitResult Hit;

	// Foot: floor in front of the ladder, at or below the bottom of the volume
	const FVector FootStart = Loc - Fwd * (Radius + 40.f);
	FVector Foot = FootStart - FVector(0, 0, HalfHeight);
	if (World->LineTraceSingleByChannel(Hit, FootStart, FootStart - FVector(0, 0, HalfHeight + 300.f), ECC_WorldStatic, Params))
	{
		Foot = Hit.ImpactPoint;
	}
	// Top: walkable ledge behind the wall, near the top of the volume
	FVector Top = Loc + Fwd * (Radius + 70.f) + FVector(0, 0, HalfHeight);
	for (const float Ahead : { Radius + 50.f, Radius + 100.f, Radius + 160.f })
	{
		const FVector Start = Loc + Fwd * Ahead + FVector(0, 0, HalfHeight + 150.f);
		if (World->LineTraceSingleByChannel(Hit, Start, Start - FVector(0, 0, HalfHeight + 150.f), ECC_WorldStatic, Params)
			&& Hit.ImpactNormal.Z > 0.7f && Hit.ImpactPoint.Z > Foot.Z + 150.f && !Hit.bStartPenetrating)
		{
			Top = Hit.ImpactPoint;
			break;
		}
	}
	LinkBottom = GetActorTransform().InverseTransformPosition(Foot);
	LinkTop = GetActorTransform().InverseTransformPosition(Top);
}
#endif

void ADFLadderVolume::OnBegin(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body, bool bSweep, const FHitResult& Hit)
{
	if (ADFCharacter* C = Cast<ADFCharacter>(Other))
	{
		// grabbed by the movement component once close enough (UDFCharacterMovement::WantsLadder)
		C->GetMFMovement()->SetLadder(this);
	}
}

void ADFLadderVolume::OnEnd(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body)
{
	if (ADFCharacter* C = Cast<ADFCharacter>(Other))
	{
		C->GetMFMovement()->ClearLadder(this);
	}
}
