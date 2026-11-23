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
	/** What SetToDefaults picks on PC. */
	static float GetDefaultFrameRateLimit() { return 120.f; }
	/** A graphics preset's levels: every group at its level, but textures one step up (2x
	 *  filtering already on Low: no measured cost, Docs/Performance.md); level 0, filtering off,
	 *  stays for Advanced Graphics. The menu and new profiles use this. */
	static Scalability::FQualityLevels PresetQuality(int32 Level);
	/** Low uses simple marks; Medium and above use full marks. */
	static int32 PresetDecalQuality(int32 Level) { return Level <= 0 ? 1 : 2; }
	/** Thirds of the screen's refresh and its half, from twice it down, above 20 FPS
	 *  then Unlimited (0): a 60 Hz screen gets 120, 100, 80, 60, 40, 30. Phones get the rates
	 *  that divide one of the panel's (the pacer holds only those): a 60 / 120 Hz one gets 120,
	 *  60, 40, 30, 24. A custom cap keeps its place. */
	static TArray<float> RecommendedFrameRates(float Saved);
	/** Phones: the frame pacer's pace for a cap (0: the default), which also picks the panel's
	 *  refresh mode. Nothing elsewhere (t.MaxFPS caps there). */
	static void ApplyFramePace(float Limit);
	UDFUserSettings();

	/** The game's settings object (the engine makes it from GameUserSettingsClassName). */
	static UDFUserSettings& Get();

	/** Player name for new games / servers (empty = "Player"). */
	UPROPERTY(Config)
	FString PlayerName;

	/** Download server for content packs (Options > Game); empty: UDFGameSettings::DownloadServer. */
	UPROPERTY(Config)
	FString DownloadServer;

	/** -1 selects the automatic model/team. Team preference applies when joining a match. */
	UPROPERTY(Config)
	int32 PreferredModel = -1;
	UPROPERTY(Config)
	int32 PreferredTeam = -1;

	/** MF Brightness (F11): scales the unshadowed ambient floor, 0.5 - 2. */
	UPROPERTY(Config)
	float Brightness = 1.f;

	/** Display gamma (UEngine::DisplayGamma): the whole picture, darks most; PC only, phones have no
	 *  tonemap pass (r.MobileHDR=False). 2.2 is the engine's default, higher is brighter. */
	UPROPERTY(Config)
	float Gamma = 2.2f;

	/** Mouse look multiplier. */
	UPROPERTY(Config)
	float MouseSensitivity = 1.f;

	UPROPERTY(Config)
	bool bInvertMouse = false;

	/** Look speed of a stick (gamepad, the touch screen's right one), a multiplier: 1 turns
	 *  150 degrees a second at full tilt, whatever the frame rate. */
	UPROPERTY(Config)
	float StickSensitivity = 1.f;

	/** Touch screens: a touch on the right half that hits no button or stick fires while held. */
	UPROPERTY(Config)
	bool bTapToFire = false;

	/** Touch buttons the player moved (SDFTouchControls' layout editor): button -> offset from
	 *  its place in Slate units. */
	UPROPERTY(Config)
	TMap<FName, FVector2D> TouchLayout;

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

	/** No blood marks (MF GameInfo.bLowGore: RageBloodSplat / RageBloodPool destroy themselves). */
	UPROPERTY(Config)
	bool bLowGore = false;

	/** Draw textures one mip down (r.MipMapLODBias 1): blurrier, for very weak GPUs (the menu's
	 *  "Blurry" texture filtering, with level 0). No preset
	 *  sets it; it saves no memory (every mip is loaded, texture streaming is off). */
	UPROPERTY(Config)
	bool bHalfSizeTextures = false;

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
	/** The hand's side along the view's Y: 1 right, -1 left, 0 centred (or hidden). */
	float WeaponHandSide() const { return WeaponHand == EDFWeaponHand::Right ? 1.f : WeaponHand == EDFWeaponHand::Left ? -1.f
																													   : 0.f; }

	/** The console keys (~ the console, Tab one line) in the shipped game too; development
	 *  builds default to enabled. */
	UPROPERTY(Config)
	bool bConsole = !UE_BUILD_SHIPPING;

	/** Keys changed in Options > Controls: action -> key (ADFPlayerController::GetBindings;
	 *  the rest keep MF's defaults). NAME_None key: unbound. */
	UPROPERTY(Config)
	TMap<FName, FKey> KeyBindings;

	virtual void ApplyNonResolutionSettings() override;
	/** Make sure the game viewport has its console (UE leaves it out of shipping builds,
	 *  ALLOW_CONSOLE, but keeps the code; chat needs its typing line) and let bConsole decide
	 *  whether the console keys open it. */
	void ApplyConsole() const;
	void ApplyGamma() const;
	/** bHalfSizeTextures over whatever the texture scalability level set. */
	void ApplyTextureSize() const;
	bool IsConsoleEnabled() const;
	virtual void SetToDefaults() override;

protected:
	/** One explicit scale relative to the actual output viewport. */
	void UpdateScreenPercentage();
};
