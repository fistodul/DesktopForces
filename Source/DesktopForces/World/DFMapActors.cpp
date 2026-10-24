#include "World/DFMapActors.h"
#include "Core/DFContent.h"
#include "Components/BoxComponent.h"
#include "DesktopForces.h"
#include "TimerManager.h"
#include "Engine/TextureRenderTargetCube.h"
#include "Components/SceneCaptureComponentCube.h"
#include "Components/AudioComponent.h"
#include "Components/BillboardComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Core/DFGameSettings.h"
#include "Core/DFUserSettings.h"
#include "Sound/SoundBase.h"

ADFMapInfo::ADFMapInfo() {
	PrimaryActorTick.bCanEverTick = false;
}

void ADFMapInfo::BeginPlay() {
	Super::BeginPlay();
	ApplyAmbient(GetWorld());
	if (Music && GetNetMode() != NM_DedicatedServer)
		MusicComponent = DF::PlayMusic(this, Music, 0.6f);
}

void ADFMapInfo::ApplyAmbient(UWorld* World) {
	UMaterialParameterCollection* MPC = World ? UDFGameSettings::Get().MaterialParameters.LoadSynchronous() : nullptr;
	if (!MPC || World->GetNetMode() == NM_DedicatedServer)
		return;
	FLinearColor Ambient = FLinearColor::Black;
	for (TActorIterator<ADFMapInfo> It(World); It; ++It)
		Ambient = It->Ambient;
	const float Scale = UDFGameSettings::Get().AmbientFloor * UDFUserSettings::Get().Brightness;
	UKismetMaterialLibrary::SetVectorParameterValue(World, MPC, TEXT("Ambient"), Ambient * Scale);
	UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, TEXT("AmbientScale"), Scale); // world: zone ambient in vertex colour
}

ADFLegacyActor::ADFLegacyActor() {
	PrimaryActorTick.bCanEverTick = false;
	SetHidden(true);
	SetActorEnableCollision(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
#if WITH_EDITORONLY_DATA
	Sprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Sprite) {
		Sprite->SetupAttachment(RootComponent);
		Sprite->bIsScreenSizeScaled = true;
	}
#endif
}

TArray<ADFLegacyActor*> ADFLegacyActor::FindAll(const UWorld* World, FName Class) {
	TArray<ADFLegacyActor*> Out;
	for (TActorIterator<ADFLegacyActor> It(World); It; ++It)
		if (It->MFClass == Class)
			Out.Add(*It);
	return Out;
}

// --- sky -------------------------------------------------------------------------------

ADFSkyCapture::ADFSkyCapture() {
	PrimaryActorTick.bCanEverTick = false;
	Capture = CreateDefaultSubobject<USceneCaptureComponentCube>(TEXT("Capture"));
	RootComponent = Capture;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
}

void ADFSkyCapture::BeginPlay() {
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer)
		return;
	UTextureRenderTargetCube* Target = LoadObject<UTextureRenderTargetCube>(nullptr,
		TEXT("/Game/MF/Materials/RT_MF_Sky.RT_MF_Sky"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Target) {
		UE_LOG(LogDF, Warning, TEXT("DFSkyCapture: RT_MF_Sky missing"));
		return;
	}
	Capture->TextureTarget = Target;
	// A few captures over the first seconds: right after load, textures are still streaming
	// in (and in editor builds shaders may still compile, which renders default materials).
	// Four 256 px cube captures in total, nothing afterwards.
	for (const float Delay : { 0.5f, 2.f, 5.f, 10.f }) {
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]() {
			Capture->CaptureScene();
		}),
			Delay, false);
	}
}

// --- zone box --------------------------------------------------------------------------

ADFZoneBox::ADFZoneBox() {
	PrimaryActorTick.bCanEverTick = false;
	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	Volume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Volume->SetMobility(EComponentMobility::Static);
	Volume->bHiddenInGame = true;
	RootComponent = Volume;
}

bool ADFZoneBox::IsInside(const FVector& Point) const {
	const FVector Local = GetActorTransform().InverseTransformPosition(Point);
	const FVector E = Volume->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= E.X && FMath::Abs(Local.Y) <= E.Y && FMath::Abs(Local.Z) <= E.Z;
}
