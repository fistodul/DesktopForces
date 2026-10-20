#include "World/MFMapActors.h"
#include "MobileForces.h"
#include "TimerManager.h"
#include "Engine/TextureRenderTargetCube.h"
#include "Components/SceneCaptureComponentCube.h"
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

// --- sky -------------------------------------------------------------------------------

AMFSkyCapture::AMFSkyCapture()
{
	PrimaryActorTick.bCanEverTick = false;
	Capture = CreateDefaultSubobject<USceneCaptureComponentCube>(TEXT("Capture"));
	RootComponent = Capture;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
}

void AMFSkyCapture::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UTextureRenderTargetCube* Target = LoadObject<UTextureRenderTargetCube>(nullptr,
		TEXT("/Game/MF/Materials/RT_MF_Sky.RT_MF_Sky"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Target)
	{
		UE_LOG(LogMF, Warning, TEXT("MFSkyCapture: RT_MF_Sky missing"));
		return;
	}
	Capture->TextureTarget = Target;
	// A few captures over the first seconds: right after load, textures are still streaming
	// in (and in editor builds shaders may still compile, which renders default materials).
	// Four 256 px cube captures in total, nothing afterwards.
	for (const float Delay : {0.5f, 2.f, 5.f, 10.f})
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			Capture->CaptureScene();
		}), Delay, false);
	}
}
