#include "UI/DFMenu.h"
#include "DesktopForces.h"
#include "Core/DFUserSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Algo/IndexOf.h"
#include "Player/DFPlayerController.h"
#include "Game/DFGameMode.h"
#include "Game/DFPlayerStart.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/CommandLine.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "EngineUtils.h"
#include "GameMapsSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Modules/ModuleManager.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Core/DFGameSettings.h"
#include "Core/DFContent.h"
#include "Engine/Texture2D.h"
#include "Sound/SoundBase.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScaleBox.h"
#if WITH_EDITOR
#include "TextureCompiler.h"
#endif
#include "Widgets/Layout/SScrollBox.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWeakWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DFMenu"

namespace
{
	// MF menu look: dark blue panels with a light border, white text
	const FLinearColor PanelColor(0.02f, 0.05f, 0.25f, 0.82f);
	const FLinearColor TitleColor(0.55f, 0.75f, 1.f);
	const TCHAR* MenuGameOption = TEXT("game=/Script/DesktopForces.DFMenuGameMode");

	FSlateFontInfo Font(int32 Size, const TCHAR* Style = TEXT("Regular"))
	{
		return FCoreStyle::GetDefaultFontStyle(Style, Size);
	}

	// the viewport owns the widget; a strong global would outlive Slate at exit (crash on Quit)
	TWeakPtr<SWidget> GMenu;
	TWeakObjectPtr<APlayerController> GOwner;
}

class SMFMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMFMenu) : _bInGame(false) {}
		SLATE_ARGUMENT(bool, bInGame)
		SLATE_ARGUMENT(TWeakObjectPtr<APlayerController>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		bInGame = Args._bInGame;
		Owner = Args._Owner;
		FindMaps();
		FindModes();
		LoadOptions();
		MakeStyles();
		// title screen: one of the team's pictures, scaled to fill; in game the match shows through
		const TArray<TSoftObjectPtr<UTexture2D>>& Pictures = UDFGameSettings::Get().MenuBackgrounds;
		if (!bInGame && Pictures.Num() > 0)
		{
			Background.Reset(Pictures[FMath::RandRange(0, Pictures.Num() - 1)].LoadSynchronous());
		}
		if (Background.IsValid())
		{
#if WITH_EDITOR
			// editor builds compile textures in the background: until then the size is a placeholder's
			FTextureCompilingManager::Get().FinishCompilation({ Background.Get() });
#endif
			BackgroundBrush.SetResourceObject(Background.Get());
			BackgroundBrush.ImageSize = FVector2D(Background->GetSizeX(), Background->GetSizeY());
		}
		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor::Black)
				.Visibility(Background.IsValid() ? EVisibility::Visible : EVisibility::Collapsed)
				.Padding(0.f)
				[
					SNew(SScaleBox).Stretch(EStretch::ScaleToFit)   // the whole framed picture, black around it
					[
						SNew(SImage).Image(&BackgroundBrush)
					]
				]
			]
			+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, bInGame ? 0.5f : 0.25f))
			]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(56.f, 40.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(LOCTEXT("Title", "DESKTOP FORCES")).Font(Font(44, TEXT("Bold")))
					.ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(3.f, 3.f)).ShadowColorAndOpacity(FLinearColor(0.02f, 0.08f, 0.35f, 0.9f))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f)
				[
					SNew(STextBlock).Text(LOCTEXT("Subtitle", "Revival")).Font(Font(15, TEXT("Italic")))
					.ColorAndOpacity(TitleColor).ShadowOffset(FVector2D(1.f, 1.f))
				]
			]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(56.f, 120.f, 56.f, 48.f)
			[
				SNew(SBorder).BorderImage(&PanelBrush).Padding(22.f)
				[
					SNew(SBox).MinDesiredWidth(470.f).MaxDesiredWidth(580.f)
					[
						SAssignNew(Switcher, SWidgetSwitcher)
						+ SWidgetSwitcher::Slot()[MakeMain()]
						+ SWidgetSwitcher::Slot()[MakePlay()]
						+ SWidgetSwitcher::Slot()[MakeJoin()]
						+ SWidgetSwitcher::Slot()[SAssignNew(OptionsHost, SBox)[MakeOptions()]]
						+ SWidgetSwitcher::Slot()[MakeCredits()]
					]
				]
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(24.f, 16.f)
			[
				SNew(STextBlock).Font(Font(10)).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.7f)).ShadowOffset(FVector2D(1.f, 1.f))
				.Text(LOCTEXT("Footer", "by fistodul, for the world's gamers  -  a fan project, not affiliated with Mobile Forces or its makers"))
			]
		];
	}

	static FString ProjectVersion()
	{
		FString Version = TEXT("0.1");
		GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Version, GGameIni);
		return Version;
	}

	void MakeStyles()
	{
		// MF-like flat blue boxes, white bold text
		PanelBrush = FSlateRoundedBoxBrush(FLinearColor(0.01f, 0.03f, 0.14f, 0.82f), 6.f, FLinearColor(0.35f, 0.55f, 1.f, 0.55f), 1.f);
		ButtonStyle = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
		ButtonStyle.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.04f, 0.10f, 0.36f, 0.9f), 4.f, FLinearColor(0.35f, 0.55f, 1.f, 0.45f), 1.f));
		ButtonStyle.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.12f, 0.28f, 0.75f, 0.95f), 4.f, FLinearColor(0.75f, 0.88f, 1.f, 0.9f), 1.f));
		ButtonStyle.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.02f, 0.06f, 0.25f, 1.f), 4.f, FLinearColor(0.75f, 0.88f, 1.f, 0.9f), 1.f));
		ButtonStyle.SetNormalForeground(FLinearColor::White);
		ButtonStyle.SetHoveredForeground(FLinearColor::White);
		ButtonStyle.SetPressedForeground(FLinearColor(0.8f, 0.9f, 1.f));
		ButtonStyle.SetNormalPadding(FMargin(0.f));
		ButtonStyle.SetPressedPadding(FMargin(0.f, 1.f, 0.f, 0.f));
	}

	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** 0-4 pages; 10-13: the Options tabs. */
	void ShowPage(int32 Page)
	{
		Switcher->SetActiveWidgetIndex(Page >= 10 ? 3 : FMath::Clamp(Page, 0, 4));
		if (Page >= 10 && OptionTabs.IsValid())
		{
			OptionTabs->SetActiveWidgetIndex(FMath::Clamp(Page - 10, 0, 3));
		}
	}
	void PressStart() { StartGame(); }

	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Key) override
	{
		if (!Capturing.IsNone())
		{
			if (Key.GetKey() != EKeys::Escape)
			{
				SetKey(Capturing, Key.GetKey());
			}
			Capturing = NAME_None;
			return FReply::Handled();
		}
		if (Key.GetKey() == EKeys::Escape)
		{
			if (Switcher->GetActiveWidgetIndex() != 0)
			{
				Switcher->SetActiveWidgetIndex(0);
			}
			else if (bInGame)
			{
				DFMenu::Close(Owner.Get());
			}
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Mouse) override
	{
		if (!Capturing.IsNone())
		{
			SetKey(Capturing, Mouse.GetEffectingButton());
			Capturing = NAME_None;
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Mouse) override
	{
		if (!Capturing.IsNone() && Mouse.GetWheelDelta() != 0.f)
		{
			SetKey(Capturing, Mouse.GetWheelDelta() > 0.f ? EKeys::MouseScrollUp : EKeys::MouseScrollDown);
			Capturing = NAME_None;
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	bool bInGame = false;
	TWeakObjectPtr<APlayerController> Owner;
	TSharedPtr<SWidgetSwitcher> Switcher;
	TStrongObjectPtr<UTexture2D> Background;
	FSlateBrush BackgroundBrush;
	FSlateBrush PanelBrush;
	FButtonStyle ButtonStyle;

	// play
	TArray<TSharedPtr<FString>> MapNames;
	TArray<FString> MapPaths;
	TSharedPtr<FString> SelectedMap;
	TArray<TSharedPtr<FString>> ModeNames;
	TArray<FString> ModePaths;
	TSharedPtr<FString> SelectedMode;
	int32 Bots = 6;
	float Skill = 0.5f;
	int32 TimeLimit = 0;
	bool bListen = false;
	FString JoinAddress = TEXT("127.0.0.1");

	// options
	FString PlayerName;
	TArray<TSharedPtr<FString>> Resolutions;
	TSharedPtr<FString> SelectedResolution;
	TArray<TSharedPtr<FString>> WindowModes;
	TSharedPtr<FString> SelectedWindowMode;
	TArray<TSharedPtr<FString>> Qualities;
	TSharedPtr<FString> SelectedQuality;
	float Brightness = 1.f;
	float Sensitivity = 1.f;
	float FOV = 100.f;
	float RenderScale = 1.f;
	bool bInvert = false;
	TArray<TSharedPtr<FString>> DecalModes;
	TSharedPtr<FString> SelectedDecals;
	float MasterVolume = 1.f;
	float MusicVolume = 1.f;
	int32 Crosshair = 13;
	float HUDSize = 2.f;
	/** Options > Controls: the keys as they'll be saved (action -> key), the one waiting for a press. */
	TMap<FName, FKey> PendingKeys;
	FName Capturing;
	TSharedPtr<SBox> OptionsHost;
	TSharedPtr<SWidgetSwitcher> OptionTabs;
	TArray<TStrongObjectPtr<UTexture2D>> CrosshairTextures;
	FSlateBrush CrosshairBrush;

	// --- helpers ----------------------------------------------------------------------
	static TSharedRef<SWidget> Heading(const FText& Text)
	{
		return SNew(STextBlock).Text(Text).Font(Font(20, TEXT("Bold"))).ColorAndOpacity(TitleColor);
	}

	TSharedRef<SWidget> Button(const FText& Text, TFunction<void()> OnClick)
	{
		return SNew(SBox).Padding(0.f, 4.f)
		[
			SNew(SButton).ButtonStyle(&ButtonStyle).HAlign(HAlign_Center).ContentPadding(FMargin(12.f, 7.f))
			.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(Text).Font(Font(16, TEXT("Bold")))
			]
		];
	}

	static TSharedRef<SWidget> Label(const FText& Text)
	{
		return SNew(SBox).Padding(0.f, 4.f, 16.f, 4.f).VAlign(VAlign_Center)
			[SNew(STextBlock).Text(Text).Font(Font(13))];
	}

	TSharedRef<SWidget> Combo(TArray<TSharedPtr<FString>>& Options, TSharedPtr<FString>& Selected)
	{
		TSharedPtr<FString>* Sel = &Selected;
		return SNew(STextComboBox).OptionsSource(&Options).InitiallySelectedItem(Selected)
			.OnSelectionChanged_Lambda([Sel](TSharedPtr<FString> Item, ESelectInfo::Type) { *Sel = Item; });
	}

	TSharedRef<SWidget> Slider(float& Value, float Min, float Max, const TCHAR* Format, float Step = 0.f)
	{
		float* V = &Value;
		const FString Fmt(Format);
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SSlider).MinValue(Min).MaxValue(Max).Value(Value)
				.StepSize(Step > 0.f ? Step : 0.01f).MouseUsesStep(Step > 0.f)
				.OnValueChanged_Lambda([V](float New) { *V = New; })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SBox).MinDesiredWidth(44.f)
				[
					SNew(STextBlock).Font(Font(12)).Text_Lambda([V, Fmt]()
					{
						return FText::FromString(Fmt == TEXT("int") ? FString::FromInt(FMath::RoundToInt(*V))
							: Fmt == TEXT("percent") ? FString::Printf(TEXT("%d%%"), FMath::RoundToInt(*V * 100.f))
							: FString::Printf(TEXT("%.2f"), *V));
					})
				]
			];
	}

	static int32 IndexOf(const TArray<TSharedPtr<FString>>& Options, const TSharedPtr<FString>& Item)
	{
		return Options.IndexOfByKey(Item);
	}

	// --- pages ------------------------------------------------------------------------
	TSharedRef<SWidget> MakeMain()
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		if (bInGame)
		{
			Box->AddSlot().AutoHeight()[Button(LOCTEXT("Resume", "Resume"), [this]() { DFMenu::Close(Owner.Get()); })];
		}
		Box->AddSlot().AutoHeight()[Button(bInGame ? LOCTEXT("NewGame", "New Game") : LOCTEXT("Play", "Play"), [this]() { Switcher->SetActiveWidgetIndex(1); })];
		Box->AddSlot().AutoHeight()[Button(LOCTEXT("Join", "Join Game"), [this]() { Switcher->SetActiveWidgetIndex(2); })];
		Box->AddSlot().AutoHeight()[Button(LOCTEXT("Options", "Options"), [this]() { Switcher->SetActiveWidgetIndex(3); })];
		Box->AddSlot().AutoHeight()[Button(LOCTEXT("Credits", "Credits"), [this]() { Switcher->SetActiveWidgetIndex(4); })];
		if (bInGame)
		{
			Box->AddSlot().AutoHeight()[Button(LOCTEXT("Leave", "Leave Match"), [this]() { DFMenu::ReturnToMainMenu(Owner.Get()); })];
		}
		Box->AddSlot().AutoHeight()[Button(LOCTEXT("Quit", "Quit"), [this]()
		{
			APlayerController* PC = Owner.Get();
			DFMenu::Close(PC);
			UKismetSystemLibrary::QuitGame(PC, PC, EQuitPreference::Quit, false);
		})];
		return Box;
	}

	TSharedRef<SWidget> MakePlay()
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)[Heading(LOCTEXT("PlayHead", "Start a game"))]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SGridPanel).FillColumn(1, 1.f)
				+ SGridPanel::Slot(0, 0)[Label(LOCTEXT("Map", "Map"))]
				+ SGridPanel::Slot(1, 0).Padding(2.f)[Combo(MapNames, SelectedMap)]
				+ SGridPanel::Slot(0, 1)[Label(LOCTEXT("Mode", "Game type"))]
				+ SGridPanel::Slot(1, 1).Padding(2.f)[Combo(ModeNames, SelectedMode)]
				+ SGridPanel::Slot(0, 2)[Label(LOCTEXT("Bots", "Bots"))]
				+ SGridPanel::Slot(1, 2).Padding(2.f)
				[
					SNew(SSpinBox<int32>).MinValue(0).MaxValue(64).Value(Bots).Font(Font(12))
					.OnValueChanged_Lambda([this](int32 V) { Bots = V; })
				]
				+ SGridPanel::Slot(0, 3)[Label(LOCTEXT("Skill", "Bot skill"))]
				+ SGridPanel::Slot(1, 3).Padding(2.f)[Slider(Skill, 0.f, 1.f, TEXT("%.2f"))]
				+ SGridPanel::Slot(0, 4)[Label(LOCTEXT("Time", "Time limit (min, 0 = default)"))]
				+ SGridPanel::Slot(1, 4).Padding(2.f)
				[
					SNew(SSpinBox<int32>).MinValue(0).MaxValue(120).Value(TimeLimit).Font(Font(12))
					.OnValueChanged_Lambda([this](int32 V) { TimeLimit = V; })
				]
				+ SGridPanel::Slot(0, 5)[Label(LOCTEXT("Listen", "Host for LAN / internet"))]
				+ SGridPanel::Slot(1, 5).Padding(2.f).HAlign(HAlign_Left)
				[
					SNew(SCheckBox).IsChecked(bListen ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bListen = S == ECheckBoxState::Checked; })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)[Button(LOCTEXT("Back", "Back"), [this]() { Switcher->SetActiveWidgetIndex(0); })]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(8.f, 0.f, 0.f, 0.f)[Button(LOCTEXT("Start", "Start"), [this]() { StartGame(); })]
			];
	}

	TSharedRef<SWidget> MakeJoin()
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)[Heading(LOCTEXT("JoinHead", "Join a game"))]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[Label(LOCTEXT("Address", "Address"))]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SEditableTextBox).Text(FText::FromString(JoinAddress)).Font(Font(13))
					.OnTextChanged_Lambda([this](const FText& T) { JoinAddress = T.ToString(); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)
			[
				SNew(STextBlock).Font(Font(11)).ColorAndOpacity(FLinearColor(0.75f, 0.8f, 0.95f)).AutoWrapText(true)
				.Text(LOCTEXT("JoinHint", "IP or host name, optionally with :port (default 7777). A server browser comes later."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)[Button(LOCTEXT("Back2", "Back"), [this]() { Switcher->SetActiveWidgetIndex(0); })]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(8.f, 0.f, 0.f, 0.f)[Button(LOCTEXT("Connect", "Connect"), [this]()
				{
					if (APlayerController* PC = Owner.Get(); PC && !JoinAddress.IsEmpty())
					{
						SaveName();
						DFMenu::Close(PC);
						PC->ClientTravel(JoinAddress + NameOption(), TRAVEL_Absolute);
					}
				})]
			];
	}

	static TSharedRef<SGridPanel> Grid() { return SNew(SGridPanel).FillColumn(1, 1.f); }

	TSharedRef<SWidget> Check(bool& Value)
	{
		bool* V = &Value;
		return SNew(SCheckBox).IsChecked(Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
			.OnCheckStateChanged_Lambda([V](ECheckBoxState S) { *V = S == ECheckBoxState::Checked; });
	}

	TSharedRef<SWidget> MakeOptions()
	{
		// Video / Audio / Controls / Game, like MF's Options window tabs
		TSharedRef<SGridPanel> Video = Grid();
		Video->AddSlot(0, 0)[Label(LOCTEXT("Res", "Resolution"))];
		Video->AddSlot(1, 0).Padding(2.f)[Combo(Resolutions, SelectedResolution)];
		Video->AddSlot(0, 1)[Label(LOCTEXT("Window", "Display"))];
		Video->AddSlot(1, 1).Padding(2.f)[Combo(WindowModes, SelectedWindowMode)];
		Video->AddSlot(0, 2)[Label(LOCTEXT("Quality", "Graphics"))];
		Video->AddSlot(1, 2).Padding(2.f)[Combo(Qualities, SelectedQuality)];
		Video->AddSlot(0, 3)[Label(LOCTEXT("RenderScale", "Render scale"))];
		Video->AddSlot(1, 3).Padding(2.f)[Slider(RenderScale, 0.25f, 2.f, TEXT("percent"), 0.25f)];
		Video->AddSlot(0, 4)[Label(LOCTEXT("Bright", "Brightness"))];
		Video->AddSlot(1, 4).Padding(2.f)[Slider(Brightness, 0.25f, 3.f, TEXT("%.2f"))];
		Video->AddSlot(0, 5)[Label(LOCTEXT("FOV", "Field of view"))];
		Video->AddSlot(1, 5).Padding(2.f)[Slider(FOV, 70.f, 130.f, TEXT("int"))];
		Video->AddSlot(0, 6)[Label(LOCTEXT("Decals", "Bullet holes, scorch marks"))];
		Video->AddSlot(1, 6).Padding(2.f)[Combo(DecalModes, SelectedDecals)];

		TSharedRef<SGridPanel> Audio = Grid();
		Audio->AddSlot(0, 0)[Label(LOCTEXT("Master", "Volume"))];
		Audio->AddSlot(1, 0).Padding(2.f)[Slider(MasterVolume, 0.f, 1.f, TEXT("percent"))];
		Audio->AddSlot(0, 1)[Label(LOCTEXT("Music", "Music"))];
		Audio->AddSlot(1, 1).Padding(2.f)[Slider(MusicVolume, 0.f, 1.f, TEXT("percent"))];

		TSharedRef<SGridPanel> Mouse = Grid();
		Mouse->AddSlot(0, 0)[Label(LOCTEXT("Mouse", "Mouse sensitivity"))];
		Mouse->AddSlot(1, 0).Padding(2.f)[Slider(Sensitivity, 0.1f, 4.f, TEXT("%.2f"))];
		Mouse->AddSlot(0, 1)[Label(LOCTEXT("Invert", "Invert mouse"))];
		Mouse->AddSlot(1, 1).Padding(2.f).HAlign(HAlign_Left)[Check(bInvert)];
		TSharedRef<SGridPanel> Keys = Grid();
		int32 Row = 0;
		for (const FDFBinding& B : ADFPlayerController::GetBindings())
		{
			Keys->AddSlot(0, Row)[Label(B.Label)];
			// the key and the second key (MF binds two to many things: Ctrl and C crouch)
			for (int32 Col = 0; Col < 2; ++Col)
			{
				const FName Slot = Col ? ADFPlayerController::AltSlot(B.Action) : B.Action;
				const FKey Default = Col ? B.AltDefault : B.Default;
				Keys->AddSlot(1 + Col, Row).Padding(2.f, 1.f)
				[
					SNew(SButton).ButtonStyle(&ButtonStyle).HAlign(HAlign_Center).ContentPadding(FMargin(8.f, 3.f))
					.OnClicked_Lambda([this, Slot]() { Capturing = Slot; FSlateApplication::Get().SetKeyboardFocus(SharedThis(this)); return FReply::Handled(); })
					[
						SNew(STextBlock).Font(Font(12)).Text_Lambda([this, Slot, Default]()
						{
							if (Capturing == Slot)
							{
								return LOCTEXT("PressKey", "press a key (Esc: keep)");
							}
							const FKey* K = PendingKeys.Find(Slot);
							const FKey Key = K ? *K : Default;
							return Key.IsValid() ? Key.GetDisplayName() : LOCTEXT("Unbound", "-");
						})
					]
				];
			}
			++Row;
		}
		TSharedRef<SVerticalBox> Controls = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Mouse]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)
			[
				SNew(STextBlock).Font(Font(11)).ColorAndOpacity(FLinearColor(0.75f, 0.8f, 0.95f)).AutoWrapText(true)
				.Text(LOCTEXT("KeysHint", "Click a key, then press the new one (a key in use swaps with it)."))
			]
			+ SVerticalBox::Slot().MaxHeight(300.f)[SNew(SScrollBox) + SScrollBox::Slot()[Keys]];

		TSharedRef<SGridPanel> Game = Grid();
		Game->AddSlot(0, 0)[Label(LOCTEXT("Name", "Name"))];
		Game->AddSlot(1, 0).Padding(2.f)
		[
			SNew(SEditableTextBox).Text(FText::FromString(PlayerName)).Font(Font(13))
			.OnTextChanged_Lambda([this](const FText& T) { PlayerName = T.ToString().Left(20); })
		];
		Game->AddSlot(0, 1)[Label(LOCTEXT("Crosshair", "Crosshair"))];
		Game->AddSlot(1, 1).Padding(2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Button(LOCTEXT("Prev", "<"), [this]() { StepCrosshair(-1); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(10.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.3f, 0.35f, 0.4f)).Padding(4.f)
				[
					SNew(SBox).WidthOverride(48.f).HeightOverride(48.f)[SNew(SImage).Image(&CrosshairBrush)]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Button(LOCTEXT("Next", ">"), [this]() { StepCrosshair(1); })]
		];
		Game->AddSlot(0, 2)[Label(LOCTEXT("HUDSize", "HUD size"))];
		Game->AddSlot(1, 2).Padding(2.f)[Slider(HUDSize, 0.f, 4.f, TEXT("int"), 1.f)];

		auto Tab = [this](const FText& Text, int32 Index)
		{
			return SNew(SBox).Padding(2.f, 0.f)
			[
				SNew(SButton).ButtonStyle(&ButtonStyle).HAlign(HAlign_Center).ContentPadding(FMargin(10.f, 4.f))
				.OnClicked_Lambda([this, Index]() { OptionTabs->SetActiveWidgetIndex(Index); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(Text).Font(Font(13, TEXT("Bold")))
					.ColorAndOpacity_Lambda([this, Index]() { return OptionTabs.IsValid() && OptionTabs->GetActiveWidgetIndex() == Index ? FSlateColor(FLinearColor::White) : FSlateColor(TitleColor); })
				]
			];
		};
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[Heading(LOCTEXT("OptHead", "Options"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)[Tab(LOCTEXT("TabVideo", "Video"), 0)]
				+ SHorizontalBox::Slot().FillWidth(1.f)[Tab(LOCTEXT("TabAudio", "Audio"), 1)]
				+ SHorizontalBox::Slot().FillWidth(1.f)[Tab(LOCTEXT("TabControls", "Controls"), 2)]
				+ SHorizontalBox::Slot().FillWidth(1.f)[Tab(LOCTEXT("TabGame", "Game"), 3)]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(OptionTabs, SWidgetSwitcher)
				+ SWidgetSwitcher::Slot()[Video]
				+ SWidgetSwitcher::Slot()[Audio]
				+ SWidgetSwitcher::Slot()[Controls]
				+ SWidgetSwitcher::Slot()[Game]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)[Button(LOCTEXT("Back3", "Back"), [this]() { Capturing = NAME_None; Switcher->SetActiveWidgetIndex(0); })]
				+ SHorizontalBox::Slot().FillWidth(1.2f).Padding(8.f, 0.f, 0.f, 0.f)[Button(LOCTEXT("Defaults", "Defaults"), [this]() { ResetOptions(); })]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(8.f, 0.f, 0.f, 0.f)[Button(LOCTEXT("Apply", "Apply"), [this]() { ApplyOptions(); })]
			];
	}

	void StepCrosshair(int32 Delta)
	{
		Crosshair = (Crosshair + Delta + 24) % 24;
		UpdateCrosshairBrush();
	}

	void UpdateCrosshairBrush()
	{
		if (CrosshairTextures.Num() == 0)
		{
			// MF CrossHairA..X (the classic pack; nothing shows without it)
			for (int32 i = 0; i < 24; ++i)
			{
				const FString Name = FString::Printf(TEXT("CrossHair%c"), TCHAR('A' + i));
				CrosshairTextures.Emplace(DF::LoadClassic<UTexture2D>(TEXT("Textures/Rage/") + Name));
			}
		}
		UTexture2D* T = CrosshairTextures.IsValidIndex(Crosshair) ? CrosshairTextures[Crosshair].Get() : nullptr;
		CrosshairBrush.SetResourceObject(T);
		CrosshairBrush.ImageSize = FVector2D(48.f);
		CrosshairBrush.TintColor = FSlateColor(FLinearColor::White);
	}

	/** Defaults: MF's options back as they ship (the window, resolution and graphics stay); Apply keeps them. */
	void ResetOptions()
	{
		const UDFUserSettings* D = GetDefault<UDFUserSettings>();
		Brightness = D->Brightness;
		Sensitivity = D->MouseSensitivity;
		bInvert = D->bInvertMouse;
		FOV = D->FieldOfView;
		RenderScale = D->RenderScale;
		SelectedDecals = DecalModes[FMath::Clamp(D->DecalQuality, 0, 2)];
		MasterVolume = D->MasterVolume;
		MusicVolume = D->MusicVolume;
		Crosshair = D->Crosshair;
		HUDSize = D->HUDSize;
		PendingKeys.Reset();
		Capturing = NAME_None;
		const int32 Tab = OptionTabs.IsValid() ? OptionTabs->GetActiveWidgetIndex() : 0;
		UpdateCrosshairBrush();
		OptionsHost->SetContent(MakeOptions());   // the widgets show the values they were made with
		OptionTabs->SetActiveWidgetIndex(Tab);
	}

	/** Give Action this key; whoever had it gets Action's old key (no key does two things). */
	/** Every key slot (each action's key and second key) with its default. */
	static TArray<TPair<FName, FKey>> KeySlots()
	{
		TArray<TPair<FName, FKey>> Out;
		for (const FDFBinding& B : ADFPlayerController::GetBindings())
		{
			Out.Add({ B.Action, B.Default });
			Out.Add({ ADFPlayerController::AltSlot(B.Action), B.AltDefault });
		}
		return Out;
	}

	void SetKey(FName Slot, const FKey& Key)
	{
		const TArray<TPair<FName, FKey>> Slots = KeySlots();
		auto Current = [this](const TPair<FName, FKey>& S) { const FKey* K = PendingKeys.Find(S.Key); return K ? *K : S.Value; };
		const TPair<FName, FKey>* Mine = Slots.FindByPredicate([Slot](const TPair<FName, FKey>& S) { return S.Key == Slot; });
		if (!Mine)
		{
			return;
		}
		const FKey Old = Current(*Mine);
		for (const TPair<FName, FKey>& S : Slots)
		{
			if (S.Key != Slot && Current(S) == Key)
			{
				PendingKeys.Add(S.Key, Old);   // a key in use swaps over
			}
		}
		PendingKeys.Add(Slot, Key);
	}

	TSharedRef<SWidget> MakeCredits()
	{
		// keep in step with CREDITS.md
		struct FLine { FText Head; FText Body; };
		const FLine Lines[] = {
			{ LOCTEXT("CrGame", "Desktop Forces"), LOCTEXT("CrGameB", "A community revival by fistodul and friends (mf.nofisto.com). Code MPL-2.0.") },
			{ LOCTEXT("CrLead", "Project lead, code"), LOCTEXT("CrLeadB", "Filip Stojanovic and the Desktop Forces contributors") },
			{ LOCTEXT("CrMods", "Mods (MF-mods)"), LOCTEXT("CrModsB", "fistodul: Totem of Undying, Trip Lasers, Seeker Mines, Sentry Gun, Tug of War, King of the Hill, Molotov") },
			{ LOCTEXT("CrModsG", "Battle Royale, Arena"), LOCTEXT("CrModsGB", "Edgaras \"Gregaras\" Ziaugra (design, Battle Royale models and textures)") },
			{ LOCTEXT("CrModsA", "Coop, Zombie modes"), LOCTEXT("CrModsAB", "Animeman (for AKVARI)") },
			{ LOCTEXT("CrModsGL", "Grenade launcher"), LOCTEXT("CrModsGLB", "Gian, Peto, \"Secret Dude\"") },
			{ LOCTEXT("CrModsMB", "DeBall, Turbo cars, mutPack"), LOCTEXT("CrModsMBB", "the community (mutPack: Gregaras)") },
			{ LOCTEXT("CrBg", "Menu pictures"), LOCTEXT("CrBgB", "Edgaras \"Gregaras\" Ziaugra") },
			{ LOCTEXT("CrMusic", "Start-up theme"), LOCTEXT("CrMusicB", "Dawid Edward Kmita") },
			{ LOCTEXT("CrMFS", "fistodul (formerly World Gamers)"), LOCTEXT("CrMFSB", "Filip Stojanovic, Junaid Malik, Edgaras Ziaugra, Ahmed Isaac Alizai, Augustinas Jatutas, Dawid Edward Kmita") },
			{ LOCTEXT("CrMaps", "Maps"), LOCTEXT("CrMapsB", "By their authors, named in each map. Classic maps and art are converted from your own copy of the original game and are not part of Desktop Forces.") },
			{ LOCTEXT("CrThanks", "Thanks"), LOCTEXT("CrThanksB", "Everyone who kept playing all these years.") },
			{ LOCTEXT("CrUE", "Unreal Engine"), LOCTEXT("CrUEB", "Desktop Forces uses the Unreal(R) Engine. Unreal(R) is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere. Unreal(R) Engine, Copyright 1998 - 2026, Epic Games, Inc. All rights reserved.") },
		};
		TSharedRef<SScrollBox> Scroll = SNew(SScrollBox);
		for (const FLine& L : Lines)
		{
			Scroll->AddSlot().Padding(0.f, 4.f, 8.f, 0.f)
			[
				SNew(STextBlock).Text(L.Head).Font(Font(13, TEXT("Bold"))).ColorAndOpacity(TitleColor)
			];
			Scroll->AddSlot().Padding(0.f, 0.f, 8.f, 6.f)
			[
				SNew(STextBlock).Text(L.Body).Font(Font(12)).AutoWrapText(true).ColorAndOpacity(FLinearColor(0.9f, 0.93f, 1.f))
			];
		}
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[Heading(FText::Format(LOCTEXT("CreditsHead", "Credits  (version {0})"), FText::FromString(ProjectVersion())))]
			+ SVerticalBox::Slot().MaxHeight(420.f)[Scroll]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
			[
				Button(LOCTEXT("Back4", "Back"), [this]() { Switcher->SetActiveWidgetIndex(0); })
			];
	}

	// --- data ---------------------------------------------------------------------------
	void FindMaps()
	{
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		// our maps, cleared community maps (core), and the classic plugin's (when installed)
		const TArray<FString> Roots = { TEXT("/Game/Maps"), TEXT("/Game/MF/Maps"), FString(DF::ClassicRoot) / TEXT("Maps") };
#if WITH_EDITOR
		AR.ScanPathsSynchronous(Roots, false);
#endif
		TArray<FAssetData> Assets;
		AR.GetAssetsByClass(UWorld::StaticClass()->GetClassPathName(), Assets);
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
		for (const FAssetData& A : Assets)
		{
			const FString Path = A.PackageName.ToString();
			if (Roots.ContainsByPredicate([&Path](const FString& R) { return Path.StartsWith(R); }))
			{
				MapPaths.Add(Path);
				MapNames.Add(MakeShared<FString>(A.AssetName.ToString()));
			}
		}
		// most wanted first: the warehouse
		const int32 Ware = MapNames.IndexOfByPredicate([](const TSharedPtr<FString>& N) { return N->Equals(TEXT("mf-warehouse"), ESearchCase::IgnoreCase); });
		SelectedMap = MapNames.IsValidIndex(Ware) ? MapNames[Ware] : (MapNames.Num() ? MapNames[0] : nullptr);
	}

	void FindModes()
	{
		struct FMode { FString Name; FString Path; };
		TArray<FMode> Modes;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->IsChildOf(ADFGameMode::StaticClass()) && *It != ADFGameMode::StaticClass()
				&& !It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
				&& !It->GetName().StartsWith(TEXT("SKEL_")) && !It->GetName().StartsWith(TEXT("REINST_")))
			{
				Modes.Add({ It->GetDefaultObject<ADFGameMode>()->GameName, It->GetPathName() });
			}
		}
		// MF's own game types in its order, then the community's (MF-mods) alphabetically
		static const TCHAR* DFOrder[] = { TEXT("DeathMatch"), TEXT("Team DM"), TEXT("Holdout"), TEXT("CTF"), TEXT("SafeCracker"),
			TEXT("Detonation"), TEXT("Trailer"), TEXT("Captains") };
		auto Rank = [](const FMode& M)
		{
			const int32 I = Algo::IndexOfByPredicate(DFOrder, [&M](const TCHAR* N) { return M.Name == N; });
			return I == INDEX_NONE ? int32(UE_ARRAY_COUNT(DFOrder)) : I;
		};
		Modes.Sort([&Rank](const FMode& A, const FMode& B)
		{
			const int32 RA = Rank(A), RB = Rank(B);
			return RA != RB ? RA < RB : A.Name < B.Name;
		});
		for (const FMode& M : Modes)
		{
			ModeNames.Add(MakeShared<FString>(M.Name));
			ModePaths.Add(M.Path);
		}
		SelectedMode = ModeNames.Num() ? ModeNames[0] : nullptr;
	}

	void LoadOptions()
	{
		UDFUserSettings* S = UDFUserSettings::Get();
		PlayerName = S && !S->PlayerName.IsEmpty() ? S->PlayerName : FString(TEXT("Player"));
		Brightness = S ? S->Brightness : 1.f;
		Sensitivity = S ? S->MouseSensitivity : 1.f;
		bInvert = S ? S->bInvertMouse : false;
		FOV = S ? S->FieldOfView : 100.f;
		RenderScale = S ? S->RenderScale : 1.f;
		DecalModes = { MakeShared<FString>(TEXT("Off")), MakeShared<FString>(TEXT("Simple")), MakeShared<FString>(TEXT("Full")) };
		SelectedDecals = DecalModes[FMath::Clamp(S ? S->DecalQuality : 2, 0, 2)];
		MasterVolume = S ? S->MasterVolume : 1.f;
		MusicVolume = S ? S->MusicVolume : 1.f;
		Crosshair = S ? S->Crosshair : 13;
		HUDSize = S ? S->HUDSize : 2;
		PendingKeys = S ? S->KeyBindings : TMap<FName, FKey>();
		UpdateCrosshairBrush();

		// what the monitor offers, plus the usual sizes of its own shape (16:10 screens get
		// 1680 x 1050, 1920 x 1200 ...), never bigger than the desktop
		TArray<FIntPoint> Supported;
		UKismetSystemLibrary::GetSupportedFullscreenResolutions(Supported);
		const FIntPoint Desktop = S ? S->GetDesktopResolution() : FIntPoint(1920, 1080);
		const float Aspect = Desktop.Y > 0 ? float(Desktop.X) / Desktop.Y : 16.f / 9.f;
		for (const int32 H : { 600, 720, 768, 800, 900, 960, 1050, 1080, 1200, 1440, 1600, 2160 })
		{
			const int32 W = FMath::RoundToInt(H * Aspect / 8.f) * 8;
			Supported.AddUnique(FIntPoint(W, H));
		}
		Supported.AddUnique(Desktop);
		Supported.RemoveAll([&Desktop](const FIntPoint& P) { return P.X > Desktop.X || P.Y > Desktop.Y || P.X < 640 || P.Y < 480; });
		Supported.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X * A.Y < B.X * B.Y; });
		const FIntPoint Current = S ? S->GetScreenResolution() : FIntPoint(1280, 720);
		Supported.AddUnique(Current);
		for (const FIntPoint& P : Supported)
		{
			Resolutions.Add(MakeShared<FString>(FString::Printf(TEXT("%d x %d"), P.X, P.Y)));
			if (P == Current)
			{
				SelectedResolution = Resolutions.Last();
			}
		}
		WindowModes = { MakeShared<FString>(TEXT("Fullscreen")), MakeShared<FString>(TEXT("Borderless window")), MakeShared<FString>(TEXT("Window")) };
		SelectedWindowMode = WindowModes[FMath::Clamp(S ? int32(S->GetFullscreenMode()) : 2, 0, 2)];
		// UE scalability 0-3; Low is the potato default (no post, small shadow maps)
		Qualities = { MakeShared<FString>(TEXT("Low")), MakeShared<FString>(TEXT("Medium")), MakeShared<FString>(TEXT("High")), MakeShared<FString>(TEXT("Epic")) };
		const int32 Q = S ? S->GetOverallScalabilityLevel() : 0;
		SelectedQuality = Qualities[FMath::Clamp(Q < 0 ? 0 : Q, 0, 3)];
	}

	FString NameOption() const
	{
		return TEXT("?Name=") + PlayerName.Replace(TEXT("?"), TEXT("")).Replace(TEXT(" "), TEXT("_"));
	}

	void SaveName()
	{
		if (UDFUserSettings* S = UDFUserSettings::Get())
		{
			S->PlayerName = PlayerName;
			S->SaveSettings();
		}
	}

	void ApplyOptions()
	{
		UDFUserSettings* S = UDFUserSettings::Get();
		if (!S)
		{
			return;
		}
		S->PlayerName = PlayerName;
		S->Brightness = Brightness;
		S->MouseSensitivity = Sensitivity;
		S->bInvertMouse = bInvert;
		S->FieldOfView = FOV;
		S->RenderScale = RenderScale;
		S->DecalQuality = FMath::Max(0, IndexOf(DecalModes, SelectedDecals));
		S->MasterVolume = MasterVolume;
		S->MusicVolume = MusicVolume;
		S->Crosshair = Crosshair;
		S->HUDSize = FMath::RoundToInt(HUDSize);
		// only what differs from MF's defaults is kept
		S->KeyBindings.Reset();
		for (const TPair<FName, FKey>& Slot : KeySlots())
		{
			if (const FKey* K = PendingKeys.Find(Slot.Key); K && *K != Slot.Value)
			{
				S->KeyBindings.Add(Slot.Key, *K);
			}
		}
		Capturing = NAME_None;
		FString W, H;
		if (SelectedResolution.IsValid() && SelectedResolution->Split(TEXT(" x "), &W, &H))
		{
			S->SetScreenResolution(FIntPoint(FCString::Atoi(*W), FCString::Atoi(*H)));
		}
		S->SetFullscreenMode(EWindowMode::ConvertIntToWindowMode(FMath::Max(0, IndexOf(WindowModes, SelectedWindowMode))));
		S->SetOverallScalabilityLevel(FMath::Max(0, IndexOf(Qualities, SelectedQuality)));
		S->ApplySettings(false);
		S->SaveSettings();
		if (APlayerController* PC = Owner.Get())
		{
			PC->ConsoleCommand(TEXT("DFApplyUserSettings"));
			if (PC->PlayerState && !PlayerName.IsEmpty() && PC->PlayerState->GetPlayerName() != PlayerName)
			{
				PC->SetName(PlayerName.Replace(TEXT("?"), TEXT("")).Left(20));   // in a game: rename now
			}
		}
		Switcher->SetActiveWidgetIndex(0);
	}

	void StartGame()
	{
		APlayerController* PC = Owner.Get();
		const int32 Map = IndexOf(MapNames, SelectedMap), Mode = IndexOf(ModeNames, SelectedMode);
		if (!PC || !MapPaths.IsValidIndex(Map) || !ModePaths.IsValidIndex(Mode))
		{
			return;
		}
		SaveName();
		FString Options = FString::Printf(TEXT("game=%s?Bots=%d?BotSkill=%.2f"), *ModePaths[Mode], Bots, Skill) + NameOption();
		if (TimeLimit > 0)
		{
			Options += FString::Printf(TEXT("?TimeLimit=%d"), TimeLimit);
		}
		if (bListen)
		{
			Options += TEXT("?listen");
		}
		UE_LOG(LogDF, Display, TEXT("Menu: open %s?%s"), *MapPaths[Map], *Options);
		DFMenu::Close(PC);
		UGameplayStatics::OpenLevel(PC, FName(*MapPaths[Map]), true, Options);
	}
};

// --- open / close ---------------------------------------------------------------------------

void DFMenu::Open(APlayerController* PC, bool bInGame)
{
	if (!PC || !PC->IsLocalController() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	Close(PC);
	TSharedRef<SMFMenu> Menu = SNew(SMFMenu).bInGame(bInGame).Owner(PC);
	GMenu = Menu;
	GOwner = PC;
	GEngine->GameViewport->AddViewportWidgetContent(Menu, 100);
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Menu);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
}

void DFMenu::Close(APlayerController* PC)
{
	if (TSharedPtr<SWidget> Menu = GMenu.Pin(); Menu && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
	}
	GMenu.Reset();
	if (PC && PC->IsLocalController() && !PC->IsA<ADFMenuPlayerController>())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
}

bool DFMenu::IsOpen()
{
	return GMenu.IsValid();
}

void DFMenu::DebugPage(int32 Page)
{
	if (TSharedPtr<SWidget> Menu = GMenu.Pin())
	{
		StaticCastSharedPtr<SMFMenu>(Menu)->ShowPage(Page);
	}
}

void DFMenu::DebugStart()
{
	if (TSharedPtr<SWidget> Menu = GMenu.Pin())
	{
		StaticCastSharedPtr<SMFMenu>(Menu)->PressStart();
	}
}

void DFMenu::ReturnToMainMenu(APlayerController* PC)
{
	if (!PC)
	{
		return;
	}
	Close(PC);
	const FString Map = GetDefault<UGameMapsSettings>()->GetGameDefaultMap();
	UGameplayStatics::OpenLevel(PC, FName(*FPackageName::ObjectPathToPackageName(Map)), true, MenuGameOption);
}

// --- title screen -------------------------------------------------------------------------

ADFMenuGameMode::ADFMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ADFMenuPlayerController::StaticClass();
	HUDClass = nullptr;
	SpectatorClass = nullptr;
}

ADFMenuPlayerController::ADFMenuPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	bAutoManageActiveCameraTarget = false;
}

void ADFMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	DFMenu::Open(this, false);
	// the start-up theme, once per run (not again when coming back from a match)
	static bool bPlayedMusic = false;
	if (!bPlayedMusic && !FParse::Param(FCommandLine::Get(), TEXT("nosound")))
	{
		if (USoundBase* Music = UDFGameSettings::Get().MenuMusic.LoadSynchronous())
		{
			bPlayedMusic = true;
			DF::PlayMusic(this, Music, 0.8f);
		}
	}
}

void ADFMenuPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	DFMenu::Close(this);
	Super::EndPlay(Reason);
}

#undef LOCTEXT_NAMESPACE
