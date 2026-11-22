#include "World/DFMapActors.h"
#include "Core/DFContent.h"
#include "Components/BoxComponent.h"
#include "DesktopForces.h"
#include "TimerManager.h"
#include "Engine/TextureRenderTargetCube.h"
#include "Engine/ExponentialHeightFog.h"
#include "Algo/BinarySearch.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "UnrealClient.h"
#include "Components/SceneCaptureComponentCube.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
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
	bUnderwater = false; // static: a map change under water would carry it over
	ApplyAmbient(GetWorld());
	if (Music && GetNetMode() != NM_DedicatedServer)
		MusicComponent = DF::PlayMusic(this, Music, 0.6f);
	if (GetNetMode() != NM_DedicatedServer && ZoneFog.Num() > 0 && FogRunEnds.Num() == FogRunValues.Num()) {
		FActorSpawnParameters SP;
		SP.ObjectFlags |= RF_Transient;
		Fog = GetWorld()->SpawnActor<AExponentialHeightFog>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
		if (UExponentialHeightFogComponent* C = Fog ? Fog->GetComponent() : nullptr) {
			// the same at every height, no sun glow, no volumetrics: UE1's flat distance fog
			C->SetFogHeightFalloff(0.f);
			C->SetFogMaxOpacity(1.f);
			C->SetDirectionalInscatteringColor(FLinearColor::Black);
			C->SetVolumetricFog(false);
			C->SetFogDensity(0.f);
		}
		UE_LOG(LogDF, Log, TEXT("Zone fog: %d fogs, grid %s, %d runs"), ZoneFog.Num(), *FogSize.ToString(), FogRunEnds.Num());
		GetWorldTimerManager().SetTimer(FogTimer, this, &ADFMapInfo::UpdateFog, 0.2f, true, 0.f);
	}
	// a map without a sky of its own: the sky windows show the fog's colour (UE1 cleared the
	// screen to it), else black, never the last map's sky (RT_MF_Sky is shared)
	if (GetNetMode() != NM_DedicatedServer && !TActorIterator<ADFSkyCapture>(GetWorld()))
		if (UTextureRenderTargetCube* Sky = LoadObject<UTextureRenderTargetCube>(nullptr, TEXT("/Game/MF/Materials/RT_MF_Sky.RT_MF_Sky"), nullptr, LOAD_NoWarn | LOAD_Quiet)) {
			Sky->ClearColor = ZoneFog.Num() > 0 ? FLinearColor(ZoneFog[0].Color) : FLinearColor::Black;
			Sky->UpdateResourceImmediate(true);
		}
}

uint8 ADFMapInfo::FogAt(const FVector& Spot) const {
	const FVector Cell = (Spot - FogOrigin) / FMath::Max(FogCell, 1.f);
	const int32 X = FMath::FloorToInt(Cell.X), Y = FMath::FloorToInt(Cell.Y), Z = FMath::FloorToInt(Cell.Z);
	if (X < 0 || Y < 0 || Z < 0 || X >= FogSize.X || Y >= FogSize.Y || Z >= FogSize.Z)
		return 255;
	const int32 Index = X + FogSize.X * (Y + FogSize.Y * Z);
	const int32 Run = Algo::UpperBound(FogRunEnds, Index);
	return FogRunValues.IsValidIndex(Run) ? FogRunValues[Run] : 255;
}

void ADFMapInfo::UpdateFog() {
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	UExponentialHeightFogComponent* C = Fog ? Fog->GetComponent() : nullptr;
	if (!PC || !PC->PlayerCameraManager || !C)
		return;
	const uint8 Zone = FogAt(PC->PlayerCameraManager->GetCameraLocation());
	if (Zone == 255 || Zone == FogZone)
		return;
	FogZone = Zone;
	UE_LOG(LogDF, Verbose, TEXT("Zone fog: %d at %s"), Zone, *PC->PlayerCameraManager->GetCameraLocation().ToCompactString());
	const FDFZoneFog* Z = ZoneFog.IsValidIndex(Zone - 1) ? &ZoneFog[Zone - 1] : nullptr;
	if (!Z) {
		C->SetFogDensity(0.f);
		return;
	}
	// UE's fog lets through exp(-(ln 2)^2 x Density / 1000 x (distance - start)) of a surface;
	// UE1's fell linearly to nothing at End. 3330 / length: the same fog on average over Start
	// to End (a fifth of a surface left at End, half at 0.43 of the way)
	C->SetStartDistance(Z->Start);
	C->SetFogDensity(FMath::Min(3330.f / FMath::Max(Z->End - Z->Start, 100.f), 10.f));
	C->SetFogInscatteringColor(FLinearColor(Z->Color));
}

bool ADFMapInfo::bUnderwater = false;

void ADFMapInfo::SetUnderwater(UWorld* World, bool bUnder) {
	if (bUnder != bUnderwater) {
		bUnderwater = bUnder;
		ApplyAmbient(World);
	}
}

ADFMapInfo* ADFMapInfo::Get(const UWorld* World) {
	TActorIterator<ADFMapInfo> It(World);
	return It ? *It : nullptr;
}

void ADFMapInfo::ApplyAmbient(UWorld* World) {
	UMaterialParameterCollection* MPC = World ? UDFGameSettings::Get().MaterialParameters.LoadSynchronous() : nullptr;
	if (!MPC || World->GetNetMode() == NM_DedicatedServer)
		return;
	const ADFMapInfo* Info = Get(World);
	const FLinearColor Ambient = Info ? Info->Ambient : FLinearColor::Black;
	// under water everything shows, in a lit blue haze: MF's zones have no shadow from the surface
	const float Scale = UDFGameSettings::Get().AmbientFloor * UDFUserSettings::Get().Brightness * (bUnderwater ? UnderwaterLight : 1.f);
	// actors and props have no zone ambient to fall back on: never quite black (UE1 AmbientGlow)
	const FLinearColor Props = Ambient * Scale;
	const float Missing = FMath::Max(0.f, UDFGameSettings::Get().ActorAmbientFloor * UDFUserSettings::Get().Brightness - Props.GetLuminance());
	UKismetMaterialLibrary::SetVectorParameterValue(World, MPC, TEXT("Ambient"), Props + FLinearColor(Missing, Missing, Missing, 0.f));
	UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, TEXT("ShadeFloor"), UDFGameSettings::Get().ShadeFloor * UDFUserSettings::Get().Brightness);
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
	// the sky zone has no fog of the viewer's (ADFMapInfo's zone fog is the view's own)
	Capture->ShowFlags.SetFog(false);
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
	// One cube face spans 90 degrees, the view FieldOfView: at 1920 px and FOV 100 a face covers
	// ~1600 screen px, so the asset's 256 px stretched the sky 6x (sawmill's moon a blur). MF drew
	// its sky live at screen resolution. Size the cube to the screen, capped by texture quality
	// (BGRA8: 512 = 6 MB, 1024 = 25 MB, 2048 = 100 MB).
	FIntPoint Screen(1920, 1080);
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
		Screen = GEngine->GameViewport->Viewport->GetSizeXY();
	const UDFUserSettings& Settings = UDFUserSettings::Get();
	const float HalfFov = FMath::DegreesToRadians(FMath::Clamp(Settings.FieldOfView, 60.f, 140.f) * 0.5f);
	const int32 Needed = FMath::RoundUpToPowerOfTwo(FMath::CeilToInt(FMath::Max(Screen.X, 1) / FMath::Tan(HalfFov)));
	const int32 TextureQuality = Settings.ScalabilityQuality.TextureQuality;
	const int32 Cap = TextureQuality <= 0 ? 512 : TextureQuality <= 2 ? 1024
																	  : 2048;
	const int32 Size = FMath::Clamp(Needed, 256, Cap);
	if (Target->SizeX != Size)
		Target->Init(Size, PF_B8G8R8A8);
	Capture->TextureTarget = Target;
	// a shell of sky round the map: holes in it (the backs of far buildings were never meant to
	// be seen through) show sky, not the black behind everything. One draw, no bake
	UStaticMesh* Shell = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* SkyMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MF/Materials/M_MF_Sky.M_MF_Sky"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Shell && SkyMaterial) {
		UStaticMeshComponent* Backstop = NewObject<UStaticMeshComponent>(this);
		Backstop->SetUsingAbsoluteLocation(true);
		Backstop->SetUsingAbsoluteScale(true);
		Backstop->SetupAttachment(RootComponent);
		Backstop->SetStaticMesh(Shell);
		Backstop->SetMaterial(0, SkyMaterial);
		Backstop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Backstop->SetCastShadow(false);
		Backstop->SetCanEverAffectNavigation(false);
		Backstop->SetWorldLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		Backstop->SetWorldScale3D(FVector(10000.f)); // 5 km round the origin
		Backstop->RegisterComponent();
		Capture->HideComponent(Backstop);
	}
	// A few captures over the first seconds: right after load, textures are still streaming
	// in (and in editor builds shaders may still compile, which renders default materials).
	// Four cube captures in total, nothing afterwards.
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
