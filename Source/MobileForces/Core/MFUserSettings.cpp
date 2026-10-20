#include "Core/MFUserSettings.h"
#include "World/MFMapActors.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

UMFUserSettings* UMFUserSettings::Get()
{
	return GEngine ? Cast<UMFUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UMFUserSettings::ApplyNonResolutionSettings()
{
	Super::ApplyNonResolutionSettings();
	Brightness = FMath::Clamp(Brightness, 0.25f, 3.f);
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
}
