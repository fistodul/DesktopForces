#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "DFUserSettings.generated.h"

/** MF Handedness: the hand the first-person weapon is held in. */
UENUM()
enum class EDFWeaponHand : uint8 {
	Right,
	Left,
	Center,
	Hidden
};

/**
 * Per-player settings (Saved/Config/<platform>/GameUserSettings.ini): UE's resolution and
 * scalability plus the MF options (name, brightness, mouse). Set as the engine's
 * GameUserSettingsClassName in DefaultEngine.ini; the main menu edits it.
 */
UCLASS()
class DESKTOPFORCES_API UDFUserSettings : public UGameUserSettings {
	GENERATED_BODY()

public:
	UDFUserSettings();

	/** The game's settings object (the engine makes it from GameUserSettingsClassName). */
	static UDFUserSettings& Get();

	/** Player name for new games / servers (empty = "Player"). */
	UPROPERTY(Config)
	FString PlayerName;

	/** -1 selects the automatic model/team. Team preference applies when joining a match. */
	UPROPERTY(Config)
	int32 PreferredModel = -1;
	UPROPERTY(Config)
	int32 PreferredTeam = -1;

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

	/** Bullet holes and scorch marks: 0 none, 1 simple (flat quads), 2 full (surface-conforming
	 *  scorch meshes). World/DFDecals.h */
	UPROPERTY(Config)
	int32 DecalQuality = 2;

	/** Everything you hear, 0 - 1 (the audio device's primary volume). */
	UPROPERTY(Config)
	float MasterVolume = 1.f;

	/** Map songs and the menu theme on top of that (MF MusicVolume), 0 - 1. */
	UPROPERTY(Config)
	float MusicVolume = 1.f;

	/** Gameplay effects and announcer voice, independent of music. */
	UPROPERTY(Config)
	float EffectsVolume = 1.f;

	UPROPERTY(Config)
	float AnnouncerVolume = 1.f;

	/** MF HUD.Crosshair: CrossHairA..X (0-23); MF's default 13. */
	UPROPERTY(Config)
	int32 Crosshair = 13;

	/** Crosshair size relative to the original HUD size. */
	UPROPERTY(Config)
	float CrosshairSize = 1.f;

	/** MF HUDSize: 0-4 = 32, 48, 64, 96, 128 px item blocks. */
	UPROPERTY(Config)
	int32 HUDSize = 2;

	/** MF Handedness (PlayerPawn.SetHand): Right, Left (the model mirrored), Center, Hidden. */
	UPROPERTY(Config)
	EDFWeaponHand WeaponHand = EDFWeaponHand::Right;

	/** The console keys (~ the console, Tab one line) in the shipped game too; development
	 *  builds default to enabled. */
	UPROPERTY(Config)
	bool bConsole = !UE_BUILD_SHIPPING;

	/** Keys changed in Options > Controls: action -> key (ADFPlayerController::GetBindings;
	 *  the rest keep MF's defaults). NAME_None key: unbound. */
	UPROPERTY(Config)
	TMap<FName, FKey> KeyBindings;

	virtual void ApplyNonResolutionSettings() override;
	/** Create or drop the game viewport's console (bConsole). UE leaves the
	 *  console out of shipping builds (ALLOW_CONSOLE) but keeps its code. */
	void ApplyConsole() const;
	bool IsConsoleEnabled() const;
	virtual void SetToDefaults() override;

protected:
	/** One explicit scale relative to the actual output viewport. */
	void UpdateScreenPercentage();
};
