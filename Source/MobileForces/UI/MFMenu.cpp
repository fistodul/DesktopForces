#include "UI/MFMenu.h"
#include "MobileForces.h"
#include "Core/MFUserSettings.h"
#include "Game/MFGameMode.h"
#include "Game/MFPlayerStart.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameMapsSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Modules/ModuleManager.h"
#include "Styling/CoreStyle.h"
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

#define LOCTEXT_NAMESPACE "MFMenu"

namespace
{
	// MF menu look: dark blue panels with a light border, white text
	const FLinearColor PanelColor(0.02f, 0.05f, 0.25f, 0.82f);
	const FLinearColor TitleColor(0.55f, 0.75f, 1.f);
	const TCHAR* MenuGameOption = TEXT("game=/Script/MobileForces.MFMenuGameMode");

	FSlateFontInfo Font(int32 Size, const TCHAR* Style = TEXT("Regular"))
	{
		return FCoreStyle::GetDefaultFontStyle(Style, Size);
	}

	TSharedPtr<SWidget> GMenu;
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
		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, bInGame ? 0.45f : 0.15f))
			]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(48.f, 36.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(LOCTEXT("Title", "MOBILE FORCES")).Font(Font(40, TEXT("Bold")))
					.ColorAndOpacity(TitleColor).ShadowOffset(FVector2D(2.f, 2.f))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(LOCTEXT("Subtitle", "Unreal Engine 5 remake")).Font(Font(14))
					.ColorAndOpacity(FLinearColor(0.8f, 0.85f, 1.f)).ShadowOffset(FVector2D(1.f, 1.f))
				]
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(PanelColor)
				.Padding(24.f)
				[
					SNew(SBox).MinDesiredWidth(460.f)
					[
						SAssignNew(Switcher, SWidgetSwitcher)
						+ SWidgetSwitcher::Slot()[MakeMain()]
						+ SWidgetSwitcher::Slot()[MakePlay()]
						+ SWidgetSwitcher::Slot()[MakeJoin()]
						+ SWidgetSwitcher::Slot()[MakeOptions()]
					]
				]
			]
		];
	}

	virtual bool SupportsKeyboardFocus() const override { return true; }

	void ShowPage(int32 Page) { Switcher->SetActiveWidgetIndex(FMath::Clamp(Page, 0, 3)); }
	void PressStart() { StartGame(); }

	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Key) override
	{
		if (Key.GetKey() == EKeys::Escape)
		{
			if (Switcher->GetActiveWidgetIndex() != 0)
			{
				Switcher->SetActiveWidgetIndex(0);
			}
			else if (bInGame)
			{
				MFMenu::Close(Owner.Get());
			}
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	bool bInGame = false;
	TWeakObjectPtr<APlayerController> Owner;
	TSharedPtr<SWidgetSwitcher> Switcher;

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
	bool bInvert = false;

	// --- helpers ----------------------------------------------------------------------
	static TSharedRef<SWidget> Heading(const FText& Text)
	{
		return SNew(STextBlock).Text(Text).Font(Font(20, TEXT("Bold"))).ColorAndOpacity(TitleColor);
	}

	TSharedRef<SWidget> Button(const FText& Text, TFunction<void()> OnClick)
	{
		return SNew(SBox).Padding(0.f, 4.f)
		[
			SNew(SButton).HAlign(HAlign_Center).ContentPadding(FMargin(12.f, 6.f))
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

	TSharedRef<SWidget> Slider(float& Value, float Min, float Max, const TCHAR* Format)
	{
		float* V = &Value;
		const FString Fmt(Format);
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SSlider).MinValue(Min).MaxValue(Max).Value(Value)
				.OnValueChanged_Lambda([V](float New) { *V = New; })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SBox).MinDesiredWidth(44.f)
				[
					SNew(STextBlock).Font(Font(12)).Text_Lambda([V, Fmt]()
					{
						return FText::FromString(Fmt == TEXT("int") ? FString::FromInt(FMath::RoundToInt(*V)) : FString::Printf(TEXT("%.2f"), *V));
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
			Box->AddSlot().AutoHeight()[Button(LOCTEXT("Resume", "Resume"), [this]() { MFMenu::Close(Owner.Get()); })];
		}
		Box->AddSlot().AutoHeight()[Button(bInGame ? LOCTEXT("NewGame", "New Game") : LOCTEXT("Play", "Play"), [this]() { Switcher->SetActiveWidgetIndex(1); })];
		Box->AddSlot().AutoHeight()[Button(LOCTEXT("Join", "Join Game"), [this]() { Switcher->SetActiveWidgetIndex(2); })];
		Box->AddSlot().AutoHeight()[Button(LOCTEXT("Options", "Options"), [this]() { Switcher->SetActiveWidgetIndex(3); })];
		if (bInGame)
		{
			Box->AddSlot().AutoHeight()[Button(LOCTEXT("Leave", "Leave Match"), [this]() { MFMenu::ReturnToMainMenu(Owner.Get()); })];
		}
		Box->AddSlot().AutoHeight()[Button(LOCTEXT("Quit", "Quit"), [this]()
		{
			UKismetSystemLibrary::QuitGame(Owner.Get(), Owner.Get(), EQuitPreference::Quit, false);
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
						MFMenu::Close(PC);
						PC->ClientTravel(JoinAddress + NameOption(), TRAVEL_Absolute);
					}
				})]
			];
	}

	TSharedRef<SWidget> MakeOptions()
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)[Heading(LOCTEXT("OptHead", "Options"))]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SGridPanel).FillColumn(1, 1.f)
				+ SGridPanel::Slot(0, 0)[Label(LOCTEXT("Name", "Name"))]
				+ SGridPanel::Slot(1, 0).Padding(2.f)
				[
					SNew(SEditableTextBox).Text(FText::FromString(PlayerName)).Font(Font(13))
					.OnTextChanged_Lambda([this](const FText& T) { PlayerName = T.ToString().Left(20); })
				]
				+ SGridPanel::Slot(0, 1)[Label(LOCTEXT("Res", "Resolution"))]
				+ SGridPanel::Slot(1, 1).Padding(2.f)[Combo(Resolutions, SelectedResolution)]
				+ SGridPanel::Slot(0, 2)[Label(LOCTEXT("Window", "Display"))]
				+ SGridPanel::Slot(1, 2).Padding(2.f)[Combo(WindowModes, SelectedWindowMode)]
				+ SGridPanel::Slot(0, 3)[Label(LOCTEXT("Quality", "Graphics"))]
				+ SGridPanel::Slot(1, 3).Padding(2.f)[Combo(Qualities, SelectedQuality)]
				+ SGridPanel::Slot(0, 4)[Label(LOCTEXT("Bright", "Brightness"))]
				+ SGridPanel::Slot(1, 4).Padding(2.f)[Slider(Brightness, 0.25f, 3.f, TEXT("%.2f"))]
				+ SGridPanel::Slot(0, 5)[Label(LOCTEXT("Mouse", "Mouse sensitivity"))]
				+ SGridPanel::Slot(1, 5).Padding(2.f)[Slider(Sensitivity, 0.1f, 4.f, TEXT("%.2f"))]
				+ SGridPanel::Slot(0, 6)[Label(LOCTEXT("Invert", "Invert mouse"))]
				+ SGridPanel::Slot(1, 6).Padding(2.f).HAlign(HAlign_Left)
				[
					SNew(SCheckBox).IsChecked(bInvert ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bInvert = S == ECheckBoxState::Checked; })
				]
				+ SGridPanel::Slot(0, 7)[Label(LOCTEXT("FOV", "Field of view"))]
				+ SGridPanel::Slot(1, 7).Padding(2.f)[Slider(FOV, 70.f, 130.f, TEXT("int"))]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)[Button(LOCTEXT("Back3", "Back"), [this]() { Switcher->SetActiveWidgetIndex(0); })]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(8.f, 0.f, 0.f, 0.f)[Button(LOCTEXT("Apply", "Apply"), [this]() { ApplyOptions(); })]
			];
	}

	// --- data ---------------------------------------------------------------------------
	void FindMaps()
	{
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		const TArray<FString> Roots = { TEXT("/Game/MFMaps"), TEXT("/Game/MF/Maps") };
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
			if (It->IsChildOf(AMFGameMode::StaticClass()) && *It != AMFGameMode::StaticClass()
				&& !It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
				&& !It->GetName().StartsWith(TEXT("SKEL_")) && !It->GetName().StartsWith(TEXT("REINST_")))
			{
				Modes.Add({ It->GetDefaultObject<AMFGameMode>()->GameName, It->GetPathName() });
			}
		}
		// MF order: deathmatch first, then the team modes alphabetically
		Modes.Sort([](const FMode& A, const FMode& B)
		{
			const bool ADM = A.Name == TEXT("DeathMatch"), BDM = B.Name == TEXT("DeathMatch");
			return ADM != BDM ? ADM : A.Name < B.Name;
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
		UMFUserSettings* S = UMFUserSettings::Get();
		PlayerName = S && !S->PlayerName.IsEmpty() ? S->PlayerName : FString(TEXT("Player"));
		Brightness = S ? S->Brightness : 1.f;
		Sensitivity = S ? S->MouseSensitivity : 1.f;
		bInvert = S ? S->bInvertMouse : false;
		FOV = S ? S->FieldOfView : 100.f;

		TArray<FIntPoint> Supported;
		UKismetSystemLibrary::GetSupportedFullscreenResolutions(Supported);
		for (const FIntPoint P : { FIntPoint(800, 600), FIntPoint(1024, 768), FIntPoint(1280, 720), FIntPoint(1366, 768),
			FIntPoint(1600, 900), FIntPoint(1920, 1080), FIntPoint(2560, 1440) })
		{
			Supported.AddUnique(P);
		}
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
		if (UMFUserSettings* S = UMFUserSettings::Get())
		{
			S->PlayerName = PlayerName;
			S->SaveSettings();
		}
	}

	void ApplyOptions()
	{
		UMFUserSettings* S = UMFUserSettings::Get();
		if (!S)
		{
			return;
		}
		S->PlayerName = PlayerName;
		S->Brightness = Brightness;
		S->MouseSensitivity = Sensitivity;
		S->bInvertMouse = bInvert;
		S->FieldOfView = FOV;
		FString W, H;
		if (SelectedResolution.IsValid() && SelectedResolution->Split(TEXT(" x "), &W, &H))
		{
			S->SetScreenResolution(FIntPoint(FCString::Atoi(*W), FCString::Atoi(*H)));
		}
		S->SetFullscreenMode(EWindowMode::ConvertIntToWindowMode(FMath::Max(0, IndexOf(WindowModes, SelectedWindowMode))));
		S->SetOverallScalabilityLevel(FMath::Max(0, IndexOf(Qualities, SelectedQuality)));
		S->ApplySettings(false);
		if (APlayerController* PC = Owner.Get())
		{
			PC->ConsoleCommand(TEXT("MFApplyUserSettings"));
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
		UE_LOG(LogMF, Display, TEXT("Menu: open %s?%s"), *MapPaths[Map], *Options);
		MFMenu::Close(PC);
		UGameplayStatics::OpenLevel(PC, FName(*MapPaths[Map]), true, Options);
	}
};

// --- open / close ---------------------------------------------------------------------------

void MFMenu::Open(APlayerController* PC, bool bInGame)
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

void MFMenu::Close(APlayerController* PC)
{
	if (GMenu.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(GMenu.ToSharedRef());
	}
	GMenu.Reset();
	if (PC && PC->IsLocalController() && !PC->IsA<AMFMenuPlayerController>())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
}

bool MFMenu::IsOpen()
{
	return GMenu.IsValid();
}

void MFMenu::DebugPage(int32 Page)
{
	if (GMenu.IsValid())
	{
		StaticCastSharedPtr<SMFMenu>(GMenu)->ShowPage(Page);
	}
}

void MFMenu::DebugStart()
{
	if (GMenu.IsValid())
	{
		StaticCastSharedPtr<SMFMenu>(GMenu)->PressStart();
	}
}

void MFMenu::ReturnToMainMenu(APlayerController* PC)
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

AMFMenuGameMode::AMFMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AMFMenuPlayerController::StaticClass();
	HUDClass = nullptr;
	SpectatorClass = nullptr;
}

AMFMenuPlayerController::AMFMenuPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	bAutoManageActiveCameraTarget = false;
}

void AMFMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	// a view of the map: from the player start that sees farthest, a bit raised, turning slowly
	FVector Loc(0.f, 0.f, 400.f);
	FRotator Rot(-10.f, 0.f, 0.f);
	float Best = -1.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFMenuView), true);
	for (TActorIterator<AMFPlayerStart> It(GetWorld()); It; ++It)
	{
		const FVector From = It->GetActorLocation() + FVector(0.f, 0.f, 220.f);   // over the props
		for (int32 Dir = 0; Dir < 8; ++Dir)
		{
			const float Yaw = Dir * 45.f;
			FHitResult Hit;
			const FVector To = From + FRotator(0.f, Yaw, 0.f).Vector() * 20000.f;
			const float Dist = GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params) ? Hit.Distance : 20000.f;
			if (Dist > Best)
			{
				Best = Dist;
				Loc = From;
				Rot = FRotator(-6.f, Yaw, 0.f);
			}
		}
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	MenuCamera = GetWorld()->SpawnActor<ACameraActor>(Loc, Rot, SP);
	if (MenuCamera)
	{
		MenuCamera->GetCameraComponent()->SetFieldOfView(90.f);
		MenuCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		SetViewTarget(MenuCamera);
	}
	MFMenu::Open(this, false);
}

void AMFMenuPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	MFMenu::Close(this);
	Super::EndPlay(Reason);
}

void AMFMenuPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (MenuCamera)
	{
		MenuCamera->AddActorWorldRotation(FRotator(0.f, 2.5f * DeltaSeconds, 0.f));
	}
}

#undef LOCTEXT_NAMESPACE
