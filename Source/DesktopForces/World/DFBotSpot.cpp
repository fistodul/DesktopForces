#include "World/DFBotSpot.h"

ADFBotSpot::ADFBotSpot()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
}
