#include "World/MFMapActors.h"
#include "Components/AudioComponent.h"
#include "Components/BillboardComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

AMFMapInfo::AMFMapInfo()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AMFMapInfo::BeginPlay()
{
	Super::BeginPlay();
	if (Music && GetNetMode() != NM_DedicatedServer)
	{
		MusicComponent = UGameplayStatics::SpawnSound2D(this, Music, 0.6f);
	}
}

AMFLegacyActor::AMFLegacyActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetHidden(true);
	SetActorEnableCollision(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
#if WITH_EDITORONLY_DATA
	Sprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Sprite)
	{
		Sprite->SetupAttachment(RootComponent);
		Sprite->bIsScreenSizeScaled = true;
	}
#endif
}

TArray<AMFLegacyActor*> AMFLegacyActor::FindAll(const UWorld* World, FName Class)
{
	TArray<AMFLegacyActor*> Out;
	for (TActorIterator<AMFLegacyActor> It(World); It; ++It)
	{
		if (It->MFClass == Class)
		{
			Out.Add(*It);
		}
	}
	return Out;
}
