#include "Core/DFUserSettings.h"
#include "UI/DFConsole.h"
#include "Engine/Console.h"
#include "Core/DFContent.h"
#include "AudioDevice.h"
#include "World/DFMapActors.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "DynamicRHI.h"
#include "HAL/PlatformFramePacer.h"
#include "DesktopForces.h"
#if PLATFORM_ANDROID
#include "Android/AndroidPlatformMisc.h"
#endif

UDFUserSettings::UDFUserSettings() {
	SetToDefaults();
}

UDFUserSettings& UDFUserSettings::Get() {
	return *CastChecked<UDFUserSettings>(GEngine->GetGameUserSettings());
}

void UDFUserSettings::ApplyNonResolutionSettings() {
	Brightness = FMath::Clamp(Brightness, 0.25f, 3.f);
	Gamma = FMath::Clamp(Gamma, 1.f, 3.f);
	ApplyGamma();
	RenderScale = FMath::Clamp(RenderScale, 0.25f, 2.f);
	CrosshairSize = FMath::Clamp(CrosshairSize, 0.25f, 2.f);
	// The advanced slider owns render scale; presets never reduce it implicitly.
	ScalabilityQuality.ResolutionQuality = RenderScale * 100.f;
	Super::ApplyNonResolutionSettings();
	ApplyFramePace(GetFrameRateLimit());
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

void UDFUserSettings::ApplyGamma() const {
	if (GEngine)
		GEngine->DisplayGamma = Gamma;
}

void UDFUserSettings::ApplyConsole() const {
	UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
	if (!VC || !GEngine->ConsoleClass)
		return;
	// the console object always exists (shipping builds leave it out): chat types on its line
	if (!VC->ViewportConsole) {
		VC->ViewportConsole = NewObject<UConsole>(VC, GEngine->ConsoleClass);
		GLog->AddOutputDevice(VC->ViewportConsole);
	}
	if (UDFConsole* Console = Cast<UDFConsole>(VC->ViewportConsole)) {
		Console->bDeveloper = bConsole;
		if (!bConsole && Console->ConsoleState != NAME_None)
			Console->FakeGotoState(NAME_None);
	}
}

bool UDFUserSettings::IsConsoleEnabled() const {
	const UDFConsole* Console = GEngine && GEngine->GameViewport ? Cast<UDFConsole>(GEngine->GameViewport->ViewportConsole) : nullptr;
	return Console && Console->bDeveloper;
}

void UDFUserSettings::UpdateScreenPercentage() {
	// UE's scalability helper clamps at 100%; allow explicit supersampling up to 200%.
	// Keep scalability priority so profiling console/command-line overrides still win.
	if (IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
		ScreenPercentage->Set(RenderScale * 100.f, ECVF_SetByScalability);
}

void UDFUserSettings::SetToDefaults() {
	Super::SetToDefaults();
#if PLATFORM_ANDROID || PLATFORM_IOS
	// phones: leave the system room (Config/Android/AndroidGameUserSettings.ini)
	SetFrameRateLimit(30.f);
	SetVSyncEnabled(true);
#else
	SetFrameRateLimit(GetDefaultFrameRateLimit());
	SetVSyncEnabled(false);
#endif
#if PLATFORM_ANDROID || PLATFORM_IOS
	ScalabilityQuality.SetFromSingleQualityLevel(0);
#else
	ScalabilityQuality.SetFromSingleQualityLevel(1); // Medium: low-end PCs can still go down
#endif
	ScalabilityQuality.ResolutionQuality = 100.f;
	PreferredModel = -1;
	PreferredTeam = -1;
	Brightness = 1.f;
	Gamma = 2.2f;
	MouseSensitivity = 1.f;
	bInvertMouse = false;
	FieldOfView = 100.f;
	RenderScale = PLATFORM_ANDROID || PLATFORM_IOS ? 0.75f : 1.f;
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

TArray<float> UDFUserSettings::RecommendedFrameRates(float Saved) {
	TArray<float> Rates;
#if PLATFORM_ANDROID || PLATFORM_IOS
	// Phones present through the frame pacer: a pace holds steady only where it divides one of
	// the panel's refresh rates (a 60 / 120 Hz Pixel: 120, 60, 40, 30, 24)
	TArray<int32> Panel;
#if PLATFORM_ANDROID
	Panel = FAndroidMisc::GetSupportedNativeDisplayRefreshRates(); // GetMaxRefreshRate stays at 60
#endif
	Panel.AddUnique(FPlatformMisc::GetMaxRefreshRate());
	for (const int32 Hz : Panel)
		for (int32 Interval = 1; Interval <= 6; ++Interval)
			if (Hz % Interval == 0 && Hz / Interval > 20)
				Rates.AddUnique(float(Hz / Interval));
	Rates.Sort(TGreater<float>());
#else
	int32 Hz = FPlatformMisc::GetMaxRefreshRate();
	FScreenResolutionArray Modes;
	// GetMaxRefreshRate is 60 on Linux: the monitor's modes
	if (RHIGetAvailableResolutions(Modes, false))
		for (const FScreenResolutionRHI& Mode : Modes)
			Hz = FMath::Max(Hz, int32(Mode.RefreshRate));
	for (const int32 Sixths : { 12, 10, 8, 6, 4, 3, 2 }) {
		const float Rate = Hz * Sixths / 6.f;
		if (Rate > 20.f)
			Rates.Add(Rate);
	}
#endif
	Rates.AddUnique(Saved);
	Rates.AddUnique(0.f);
	return Rates;
}

void UDFUserSettings::ApplyFramePace(float Limit) {
#if PLATFORM_ANDROID || PLATFORM_IOS
	// t.MaxFPS only sleeps: the pacer (Swappy) holds the pace and picks the panel mode, else a
	// phone stays at its 60 Hz default whatever the cap
	const int32 Pace = FPlatformRHIFramePacer::SetFramePace(Limit > 0.f ? FMath::RoundToInt(Limit) : 0);
	UE_LOG(LogDF, Log, TEXT("Frame pace %.0f -> %d"), Limit, Pace);
#endif
}
