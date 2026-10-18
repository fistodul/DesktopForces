#include "World/MFLadder.h"
#include "Player/MFCharacter.h"
#include "Player/MFCharacterMovement.h"
#include "Components/CapsuleComponent.h"

AMFLadderVolume::AMFLadderVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	Volume = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Volume"));
	Volume->SetCollisionProfileName(TEXT("Trigger"));
	Volume->SetGenerateOverlapEvents(true);
	Volume->SetMobility(EComponentMobility::Static);
	Volume->bHiddenInGame = true;
	RootComponent = Volume;
}

void AMFLadderVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Volume->SetCapsuleSize(Radius, HalfHeight);
}

void AMFLadderVolume::BeginPlay()
{
	Super::BeginPlay();
	Volume->SetCapsuleSize(Radius, HalfHeight);
	Volume->OnComponentBeginOverlap.AddDynamic(this, &AMFLadderVolume::OnBegin);
	Volume->OnComponentEndOverlap.AddDynamic(this, &AMFLadderVolume::OnEnd);
}

void AMFLadderVolume::OnBegin(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body, bool bSweep, const FHitResult& Hit)
{
	if (AMFCharacter* C = Cast<AMFCharacter>(Other))
	{
		// The climber faces the wall: the wall's normal points back along -Forward
		C->GetMFMovement()->EnterLadder(-GetActorForwardVector(), ClimbSpeed);
	}
}

void AMFLadderVolume::OnEnd(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body)
{
	if (AMFCharacter* C = Cast<AMFCharacter>(Other))
	{
		C->GetMFMovement()->ExitLadder();
	}
}
