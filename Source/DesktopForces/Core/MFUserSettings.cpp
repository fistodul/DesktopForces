#include "Core/MFUserSettings.h"
#include "World/MFMapActors.h"
#include "Engine/Engine.h"
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
	if (IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		ScreenPercentage->Set(RenderScale * 100.f, ECVF_SetByGameSetting);
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

void UMFUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	Brightness = 1.f;
	MouseSensitivity = 1.f;
	bInvertMouse = false;
	FieldOfView = 100.f;
	RenderScale = 1.f;
}
