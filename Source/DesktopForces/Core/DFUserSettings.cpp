#include "Core/DFUserSettings.h"
#include "Engine/Console.h"
#include "Core/DFContent.h"
#include "AudioDevice.h"
#include "World/DFMapActors.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

UDFUserSettings::UDFUserSettings() {
	SetToDefaults();
}

UDFUserSettings& UDFUserSettings::Get() {
	return *CastChecked<UDFUserSettings>(GEngine->GetGameUserSettings());
}

void UDFUserSettings::ApplyNonResolutionSettings() {
	Brightness = FMath::Clamp(Brightness, 0.25f, 3.f);
	RenderScale = FMath::Clamp(RenderScale, 0.25f, 2.f);
	CrosshairSize = FMath::Clamp(CrosshairSize, 0.25f, 2.f);
	// The advanced slider owns render scale; presets never reduce it implicitly.
	ScalabilityQuality.ResolutionQuality = RenderScale * 100.f;
	Super::ApplyNonResolutionSettings();
	UpdateScreenPercentage();
	MasterVolume = FMath::Clamp(MasterVolume, 0.f, 1.f);
	MusicVolume = FMath::Clamp(MusicVolume, 0.f, 1.f);
	EffectsVolume = FMath::Clamp(EffectsVolume, 0.f, 1.f);
	AnnouncerVolume = FMath::Clamp(AnnouncerVolume, 0.f, 1.f);
	DF::UpdateSoundVolumes();
	if (GEngine) {
		if (FAudioDeviceHandle Audio = GEngine->GetMainAudioDevice())
			Audio->SetTransientPrimaryVolume(MasterVolume);
		DF::UpdateMusicVolume();
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (Context.World() && Context.World()->IsGameWorld())
				ADFMapInfo::ApplyAmbient(Context.World());
	}
	ApplyConsole();
}

void UDFUserSettings::ApplyConsole() const {
	UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
	if (!VC || !GEngine->ConsoleClass)
		return;
	if (bConsole && !VC->ViewportConsole) {
		VC->ViewportConsole = NewObject<UConsole>(VC, GEngine->ConsoleClass);
		GLog->AddOutputDevice(VC->ViewportConsole);
	} else if (!bConsole && VC->ViewportConsole) {
		GLog->RemoveOutputDevice(VC->ViewportConsole);
		VC->ViewportConsole = nullptr;
	}
}

bool UDFUserSettings::IsConsoleEnabled() const {
	return GEngine && GEngine->GameViewport && GEngine->GameViewport->ViewportConsole;
}

void UDFUserSettings::UpdateScreenPercentage() {
	// UE's scalability helper clamps at 100%; allow explicit supersampling up to 200%.
	// Keep scalability priority so profiling console/command-line overrides still win.
	if (IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
		ScreenPercentage->Set(RenderScale * 100.f, ECVF_SetByScalability);
}

void UDFUserSettings::SetToDefaults() {
	Super::SetToDefaults();
	SetFrameRateLimit(120.f);
	SetVSyncEnabled(false);
	ScalabilityQuality.SetFromSingleQualityLevel(0);
	ScalabilityQuality.ResolutionQuality = 100.f;
	PreferredModel = -1;
	PreferredTeam = -1;
	Brightness = 1.f;
	MouseSensitivity = 1.f;
	bInvertMouse = false;
	FieldOfView = 100.f;
	RenderScale = 1.f;
	DecalQuality = 2;
	MasterVolume = 1.f;
	MusicVolume = 1.f;
	EffectsVolume = 1.f;
	AnnouncerVolume = 1.f;
	Crosshair = 13;
	CrosshairSize = 1.f;
	HUDSize = 2;
	WeaponHand = EDFWeaponHand::Right;
	bConsole = !UE_BUILD_SHIPPING;
	KeyBindings.Reset();
}
