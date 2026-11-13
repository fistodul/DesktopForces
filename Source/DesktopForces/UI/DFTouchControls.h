#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ADFPlayerController;

/**
 * Touch buttons over the game view. The two sticks are UE's virtual joysticks
 * (Input settings DefaultTouchInterface: left moves, right looks, as the gamepad sticks); these
 * buttons hold the controller's input actions down while touched
 * (ADFPlayerController::TouchAction), so they act exactly like their keys.
 * Shown where the platform uses virtual joysticks, or with bAlwaysShowTouchInterface.
 *
 * Each button shows a drawn sign (lines, no texture: sharp at any size, nothing to ship) and
 * sits where the layout puts it plus the player's own offset (UDFUserSettings::TouchLayout).
 * SetEditing(true): the buttons stop acting and can be dragged; Done saves, Reset puts them back.
 */
class DESKTOPFORCES_API SDFTouchControls : public SCompoundWidget {
public:
	SLATE_BEGIN_ARGS(SDFTouchControls) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, ADFPlayerController* InController);

	void SetEditing(bool bOn);
	bool IsEditing() const { return bEditing; }
	/** A button's offset from its place, in Slate units (scripted tests, the drag handles). */
	void MoveButton(FName Button, const FVector2D& Offset);
	FVector2D GetOffset(FName Button) const { return Offsets.FindRef(Button); }
	/** Every button's name (the keys of UDFUserSettings::TouchLayout). */
	const TArray<FName>& GetButtons() const { return Buttons; }

private:
	TWeakObjectPtr<ADFPlayerController> Controller;
	TMap<FName, FVector2D> Offsets;
	TArray<FName> Buttons;
	bool bEditing = false;

	EVisibility PlayButtonsVisibility() const;
	/** A see-through button holding Action down (or running Command on a tap). */
	TSharedRef<SWidget> Button(FName Sign, FName Action, float Size, const TCHAR* Command = nullptr);
	void Save();
};
