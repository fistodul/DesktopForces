#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "MFUserSettings.generated.h"

/**
 * Per-player settings (Saved/Config/<platform>/GameUserSettings.ini): UE's resolution and
 * scalability plus the MF options (name, brightness, mouse). Set as the engine's
 * GameUserSettingsClassName in DefaultEngine.ini; the main menu edits it.
 */
UCLASS()
class MOBILEFORCES_API UMFUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	static UMFUserSettings* Get();

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

	/** Horizontal field of view in degrees (AMFCharacter::DefaultFOV 100). */
	UPROPERTY(Config)
	float FieldOfView = 100.f;

	/**
	 * 3D render resolution relative to the window (r.ScreenPercentage / 100), 0.25 - 2, the HUD
	 * stays sharp. Below 1 for very weak GPUs, above 1 supersamples. 1 costs nothing extra.
	 */
	UPROPERTY(Config)
	float RenderScale = 1.f;

	virtual void ApplyNonResolutionSettings() override;
	virtual void SetToDefaults() override;
};
