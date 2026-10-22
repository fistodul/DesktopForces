#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "DFUserSettings.generated.h"

/**
 * Per-player settings (Saved/Config/<platform>/GameUserSettings.ini): UE's resolution and
 * scalability plus the MF options (name, brightness, mouse). Set as the engine's
 * GameUserSettingsClassName in DefaultEngine.ini; the main menu edits it.
 */
UCLASS()
class DESKTOPFORCES_API UDFUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	/** The game's settings object (the engine makes it from GameUserSettingsClassName). */
	static UDFUserSettings& Get();

	/** Player name for new games / servers (empty = "Player"). */
	UPROPERTY(Config)
	FString PlayerName;

	/** MF Brightness (F11): scales the unshadowed ambient floor, 0.5 - 2. */
	UPROPERTY(Config)
	float Brightness = 1.f;

	/** Mouse look multiplier. */
	UPROPERTY(Config)
	float MouseSensitivity = 1.f;

	UPROPERTY(Config)
	bool bInvertMouse = false;

	/** Horizontal field of view in degrees (ADFCharacter::DefaultFOV 100). */
	UPROPERTY(Config)
	float FieldOfView = 100.f;

	/**
	 * 3D render resolution relative to the window (r.ScreenPercentage / 100), 0.25 - 2, the HUD
	 * stays sharp. Below 1 for very weak GPUs, above 1 supersamples. 1 costs nothing extra.
	 */
	UPROPERTY(Config)
	float RenderScale = 1.f;

	/** Bullet holes and scorch marks: 0 none, 1 simple (flat quads), 2 full (scorch marks as
	 *  projected decals). World/DFDecals.h */
	UPROPERTY(Config)
	int32 DecalQuality = 2;

	/** Everything you hear, 0 - 1 (the audio device's primary volume). */
	UPROPERTY(Config)
	float MasterVolume = 1.f;

	/** Map songs and the menu theme on top of that (MF MusicVolume), 0 - 1. */
	UPROPERTY(Config)
	float MusicVolume = 1.f;

	/** MF HUD.Crosshair: CrossHairA..X (0-23); MF's default 13. */
	UPROPERTY(Config)
	int32 Crosshair = 13;

	/** MF HUDSize: 0-4 = 32, 48, 64, 96, 128 px item blocks. */
	UPROPERTY(Config)
	int32 HUDSize = 2;

	/** Keys changed in Options > Controls: action -> key (ADFPlayerController::GetBindings;
	 *  the rest keep MF's defaults). NAME_None key: unbound. */
	UPROPERTY(Config)
	TMap<FName, FKey> KeyBindings;

	virtual void ApplyNonResolutionSettings() override;
	virtual void SetToDefaults() override;

protected:
	/** r.ScreenPercentage from RenderScale and the resolution (UpdateScreenPercentage). */
	void UpdateScreenPercentage();
	FDelegateHandle ResizedHandle;
};
