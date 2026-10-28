#include "UI/DFTouchControls.h"
#include "Player/DFPlayerController.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DFTouch"

void SDFTouchControls::Construct(const FArguments& InArgs, ADFPlayerController* InController) {
	Controller = InController;
	// the virtual joysticks sit in the bottom corners: buttons go above the right one, along
	// the bottom between them and along the top
	// clang-format off
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0.f, 0.f, 40.f, 60.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(4.f)[Button(LOCTEXT("Fire", "FIRE"), TEXT("Fire"), 150.f)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(LOCTEXT("Reload", "RELOAD"), TEXT("Reload"), 90.f)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(LOCTEXT("Alt", "ALT"), TEXT("AltFire"), 100.f)]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 110.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f)[Button(LOCTEXT("Crouch", "CROUCH"), TEXT("Crouch"), 100.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f)[Button(LOCTEXT("Use", "USE"), TEXT("Use"), 100.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f)[Button(LOCTEXT("Jump", "JUMP"), TEXT("Jump"), 110.f)]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.f, 20.f, 20.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(LOCTEXT("Prev", "<"), TEXT("PrevWeapon"), 80.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(LOCTEXT("Next", ">"), TEXT("NextWeapon"), 80.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(LOCTEXT("Loadout", "LOADOUT"), TEXT("Loadout"), 90.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(LOCTEXT("Scores", "SCORES"), TEXT("Scores"), 80.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(LOCTEXT("Menu", "MENU"), NAME_None, 80.f, TEXT("ShowMenu"))]
		]
	];
	// clang-format on
}

TSharedRef<SWidget> SDFTouchControls::Button(const FText& Label, FName Action, float Size, const TCHAR* Command) {
	TWeakObjectPtr<ADFPlayerController> PC = Controller;
	const FString Cmd = Command ? Command : TEXT("");
	// clang-format off
	return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
	[
		SNew(SButton)
		.IsFocusable(false) // the game view keeps the keyboard
		.TouchMethod(EButtonTouchMethod::DownAndUp)
		.ButtonColorAndOpacity(FLinearColor(0.1f, 0.2f, 0.6f, 0.35f))
		.HAlign(HAlign_Center).VAlign(VAlign_Center)
		.OnPressed_Lambda([PC, Action, Cmd]() {
			if (!PC.IsValid())
				return;
			if (!Cmd.IsEmpty())
				PC->ConsoleCommand(Cmd);
			else
				PC->TouchAction(Action, true);
		})
		.OnReleased_Lambda([PC, Action, Cmd]() {
			if (PC.IsValid() && Cmd.IsEmpty())
				PC->TouchAction(Action, false);
		})
		[
			SNew(STextBlock).Text(Label).Font(FCoreStyle::GetDefaultFontStyle("Bold", FMath::RoundToInt(Size / 9.5f)))
			.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.8f))
		]
	];
	// clang-format on
}

#undef LOCTEXT_NAMESPACE
