#include "Core/MFUserSettings.h"
#include "World/MFMapActors.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

UMFUserSettings* UMFUserSettings::Get()
{
	return GEngine ? Cast<UMFUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UMFUserSettings::ApplyNonResolutionSettings()
{
	Super::ApplyNonResolutionSettings();
	Brightness = FMath::Clamp(Brightness, 0.25f, 3.f);
	RenderScale = FMath::Clamp(RenderScale, 0.25f, 2.f);
	UpdateScreenPercentage();
	if (!ResizedHandle.IsValid())
	{
		// the window may only get its new size a frame later (or never: borderless stays desktop size)
		ResizedHandle = FViewport::ViewportResizedEvent.AddWeakLambda(this, [this](FViewport*, uint32) { UpdateScreenPercentage(); });
	}
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				AMFMapInfo::ApplyAmbient(Context.World());
			}
		}
	}
}

void UMFUserSettings::UpdateScreenPercentage()
{
	// Render scale x the chosen resolution: in a borderless window (and fullscreen where the
	// desktop can't switch modes) the window keeps the desktop size, so a smaller resolution
	// renders smaller and is scaled up, as a real mode switch would look
	float Scale = RenderScale;
	const FViewport* Viewport = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
	const FIntPoint Res = GetScreenResolution();
	if (Viewport && GetFullscreenMode() != EWindowMode::Windowed && Res.Y > 0 && Viewport->GetSizeXY().Y > Res.Y)
	{
		Scale *= float(Res.Y) / float(Viewport->GetSizeXY().Y);
	}
	if (IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		ScreenPercentage->Set(FMath::Clamp(Scale, 0.1f, 2.f) * 100.f, ECVF_SetByGameSetting);
	}
}

void UMFUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	Brightness = 1.f;
	MouseSensitivity = 1.f;
	bInvertMouse = false;
	FieldOfView = 100.f;
	RenderScale = 1.f;
	DecalQuality = 2;
}
