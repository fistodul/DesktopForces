#include "Core/DFUserSettings.h"
#include "Core/DFContent.h"
#include "AudioDevice.h"
#include "World/DFMapActors.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

UDFUserSettings* UDFUserSettings::Get()
{
	return GEngine ? Cast<UDFUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UDFUserSettings::ApplyNonResolutionSettings()
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
	MasterVolume = FMath::Clamp(MasterVolume, 0.f, 1.f);
	MusicVolume = FMath::Clamp(MusicVolume, 0.f, 1.f);
	if (GEngine)
	{
		if (FAudioDeviceHandle Audio = GEngine->GetMainAudioDevice())
		{
			Audio->SetTransientPrimaryVolume(MasterVolume);
		}
		DF::UpdateMusicVolume();
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				ADFMapInfo::ApplyAmbient(Context.World());
			}
		}
	}
}

void UDFUserSettings::UpdateScreenPercentage()
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

void UDFUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	Brightness = 1.f;
	MouseSensitivity = 1.f;
	bInvertMouse = false;
	FieldOfView = 100.f;
	RenderScale = 1.f;
	DecalQuality = 2;
	MasterVolume = 1.f;
	MusicVolume = 1.f;
	Crosshair = 13;
	HUDSize = 2;
	KeyBindings.Reset();
}
