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
 */
class DESKTOPFORCES_API SDFTouchControls : public SCompoundWidget {
public:
	SLATE_BEGIN_ARGS(SDFTouchControls) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, ADFPlayerController* InController);

private:
	TWeakObjectPtr<ADFPlayerController> Controller;
	/** A round, see-through button holding Action down (or running Command on a tap). */
	TSharedRef<SWidget> Button(const FText& Label, FName Action, float Size, const TCHAR* Command = nullptr);
};
