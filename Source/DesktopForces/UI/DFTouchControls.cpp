#include "UI/DFTouchControls.h"
#include "Core/DFUserSettings.h"
#include "Player/DFPlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DFTouch"

namespace {
	using FStroke = TArray<FVector2f>;

	FStroke Arc(float Radius, float FromDeg, float ToDeg, const FVector2f& Centre = FVector2f::ZeroVector) {
		FStroke Out;
		const int32 Steps = FMath::Max(4, FMath::CeilToInt(FMath::Abs(ToDeg - FromDeg) / 15.f));
		for (int32 I = 0; I <= Steps; ++I) {
			const float A = FMath::DegreesToRadians(FMath::Lerp(FromDeg, ToDeg, float(I) / Steps));
			Out.Add(Centre + FVector2f(FMath::Cos(A), FMath::Sin(A)) * Radius);
		}
		return Out;
	}

	/** A button's sign as strokes in a -1..1 square, Y down. */
	TArray<FStroke> SignStrokes(FName Sign) {
		TArray<FStroke> S;
		auto Line = [&S](float X0, float Y0, float X1, float Y1) { S.Add({ FVector2f(X0, Y0), FVector2f(X1, Y1) }); };
		if (Sign == TEXT("Fire")) { // a crosshair
			S.Add(Arc(0.6f, 0.f, 360.f));
			Line(-1.f, 0.f, -0.25f, 0.f);
			Line(0.25f, 0.f, 1.f, 0.f);
			Line(0.f, -1.f, 0.f, -0.25f);
			Line(0.f, 0.25f, 0.f, 1.f);
		} else if (Sign == TEXT("AltFire")) { // a scope: ring in a ring
			S.Add(Arc(0.9f, 0.f, 360.f));
			S.Add(Arc(0.35f, 0.f, 360.f));
		} else if (Sign == TEXT("Reload")) { // round arrow
			S.Add(Arc(0.75f, -60.f, 240.f));
			S.Add({ FVector2f(0.05f, -0.95f), FVector2f(0.4f, -0.65f), FVector2f(0.0f, -0.3f) });
		} else if (Sign == TEXT("Jump")) { // up
			S.Add({ FVector2f(-0.7f, 0.f), FVector2f(0.f, -0.7f), FVector2f(0.7f, 0.f) });
			S.Add({ FVector2f(-0.7f, 0.7f), FVector2f(0.f, 0.f), FVector2f(0.7f, 0.7f) });
		} else if (Sign == TEXT("Crouch")) { // down
			S.Add({ FVector2f(-0.7f, -0.7f), FVector2f(0.f, 0.f), FVector2f(0.7f, -0.7f) });
			S.Add({ FVector2f(-0.7f, 0.f), FVector2f(0.f, 0.7f), FVector2f(0.7f, 0.f) });
		} else if (Sign == TEXT("Use")) { // into a doorway
			S.Add({ FVector2f(0.1f, -0.9f), FVector2f(0.9f, -0.9f), FVector2f(0.9f, 0.9f), FVector2f(0.1f, 0.9f) });
			Line(-0.9f, 0.f, 0.45f, 0.f);
			S.Add({ FVector2f(0.05f, -0.4f), FVector2f(0.45f, 0.f), FVector2f(0.05f, 0.4f) });
		} else if (Sign == TEXT("PrevWeapon")) {
			S.Add({ FVector2f(0.5f, -0.8f), FVector2f(-0.5f, 0.f), FVector2f(0.5f, 0.8f) });
		} else if (Sign == TEXT("NextWeapon")) {
			S.Add({ FVector2f(-0.5f, -0.8f), FVector2f(0.5f, 0.f), FVector2f(-0.5f, 0.8f) });
		} else if (Sign == TEXT("Loadout")) { // a crate
			S.Add({ FVector2f(-0.85f, -0.7f), FVector2f(0.85f, -0.7f), FVector2f(0.85f, 0.7f), FVector2f(-0.85f, 0.7f), FVector2f(-0.85f, -0.7f) });
			Line(-0.85f, -0.25f, 0.85f, -0.25f);
			Line(-0.2f, -0.25f, -0.2f, 0.1f);
			Line(0.2f, -0.25f, 0.2f, 0.1f);
			Line(-0.2f, 0.1f, 0.2f, 0.1f);
		} else if (Sign == TEXT("Scores")) { // a chart
			Line(-0.6f, 0.8f, -0.6f, 0.1f);
			Line(0.f, 0.8f, 0.f, -0.8f);
			Line(0.6f, 0.8f, 0.6f, -0.3f);
			Line(-0.95f, 0.8f, 0.95f, 0.8f);
		} else { // Menu: three bars
			Line(-0.8f, -0.6f, 0.8f, -0.6f);
			Line(-0.8f, 0.f, 0.8f, 0.f);
			Line(-0.8f, 0.6f, 0.8f, 0.6f);
		}
		return S;
	}

	/** Draws a sign; takes no touches (the button under it does). */
	class SDFTouchSign : public SLeafWidget {
	public:
		SLATE_BEGIN_ARGS(SDFTouchSign) {}
		SLATE_ARGUMENT(FName, Sign)
		SLATE_END_ARGS()

		void Construct(const FArguments& Args) {
			Strokes = SignStrokes(Args._Sign);
			SetVisibility(EVisibility::HitTestInvisible);
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(48.f, 48.f); }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override {
			const FVector2f Size = Geometry.GetLocalSize();
			const FVector2f Centre = Size * 0.5f;
			const float Half = FMath::Min(Size.X, Size.Y) * 0.5f;
			for (const FStroke& Stroke : Strokes) {
				TArray<FVector2f> Points;
				Points.Reserve(Stroke.Num());
				for (const FVector2f& P : Stroke)
					Points.Add(Centre + P * Half);
				FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.85f), true, FMath::Max(2.f, Half * 0.14f));
			}
			return Layer;
		}

	private:
		TArray<FStroke> Strokes;
	};

	/** Over a button while the layout is edited: drags it about. */
	class SDFTouchHandle : public SLeafWidget {
	public:
		SLATE_BEGIN_ARGS(SDFTouchHandle) {}
		SLATE_EVENT(FSimpleDelegate, OnDropped)
		SLATE_END_ARGS()

		void Construct(const FArguments& Args, TFunction<void(const FVector2D&)> InDrag) {
			Drag = MoveTemp(InDrag);
			Dropped = Args._OnDropped;
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override {
			// a dashed-looking frame: this one can be moved
			const FVector2f S = Geometry.GetLocalSize();
			const TArray<FVector2f> Frame = { FVector2f(1.f, 1.f), FVector2f(S.X - 1.f, 1.f), FVector2f(S.X - 1.f, S.Y - 1.f), FVector2f(1.f, S.Y - 1.f), FVector2f(1.f, 1.f) };
			FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Frame, ESlateDrawEffect::None, FLinearColor(1.f, 0.85f, 0.2f, HasMouseCapture() ? 1.f : 0.6f), true, 2.f);
			return Layer;
		}

		// touches arrive as these too (Slate hands unhandled touches on as mouse events)
		virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override {
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}

		virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override {
			if (!HasMouseCapture())
				return FReply::Unhandled();
			// screen pixels into this widget's units (DPI scale, the safe zone's none)
			const float Scale = FMath::Max(0.01f, Geometry.GetAccumulatedLayoutTransform().GetScale());
			Drag(FVector2D(Event.GetCursorDelta()) / Scale);
			return FReply::Handled();
		}

		virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override {
			if (!HasMouseCapture())
				return FReply::Unhandled();
			Dropped.ExecuteIfBound();
			return FReply::Handled().ReleaseMouseCapture();
		}

	private:
		TFunction<void(const FVector2D&)> Drag;
		FSimpleDelegate Dropped;
	};

	constexpr float MaxOffset = 3000.f; // a saved offset can't lose a button for good
} // namespace

void SDFTouchControls::Construct(const FArguments& InArgs, ADFPlayerController* InController) {
	Controller = InController;
	Offsets = UDFUserSettings::Get().TouchLayout;
	// only the buttons take touches: everything else goes on to the virtual joysticks (a
	// sibling underneath) and the game view
	SetVisibility(EVisibility::SelfHitTestInvisible);
	// the virtual joysticks sit in the bottom corners: buttons go above the right one, along
	// the bottom between them and along the top
	// clang-format off
	ChildSlot
	[
		SNew(SSafeZone).Visibility(EVisibility::SelfHitTestInvisible) // clear of the camera hole and rounded corners
		[
		SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0.f, 0.f, 40.f, 60.f)
		[
			SNew(SVerticalBox).Visibility(this, &SDFTouchControls::PlayButtonsVisibility)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(4.f)[Button(TEXT("Fire"), TEXT("Fire"), 200.f)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(4.f)
			[
				SNew(SHorizontalBox).Visibility(EVisibility::SelfHitTestInvisible)
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(TEXT("Reload"), TEXT("Reload"), 120.f)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(TEXT("AltFire"), TEXT("AltFire"), 130.f)]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 110.f)
		[
			SNew(SHorizontalBox).Visibility(this, &SDFTouchControls::PlayButtonsVisibility)
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f)[Button(TEXT("Crouch"), TEXT("Crouch"), 130.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f)[Button(TEXT("Use"), TEXT("Use"), 130.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f)[Button(TEXT("Jump"), TEXT("Jump"), 140.f)]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.f, 20.f, 20.f, 0.f)
		[
			SNew(SHorizontalBox).Visibility(EVisibility::SelfHitTestInvisible)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(TEXT("PrevWeapon"), TEXT("PrevWeapon"), 110.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(TEXT("NextWeapon"), TEXT("NextWeapon"), 110.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(TEXT("Loadout"), TEXT("Loadout"), 120.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(TEXT("Scores"), TEXT("Scores"), 110.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(TEXT("Menu"), NAME_None, 110.f, TEXT("ShowMenu"))]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.f, 150.f, 0.f, 0.f)
		[
			// the layout editor's own bar
			SNew(SBorder).Visibility_Lambda([this]() { return bEditing ? EVisibility::Visible : EVisibility::Collapsed; })
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.01f, 0.03f, 0.14f, 0.85f)).Padding(12.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 16.f, 0.f)
				[
					SNew(STextBlock).Text(LOCTEXT("EditHint", "Drag the buttons where you want them")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
				[
					SNew(SButton).IsFocusable(false).ContentPadding(FMargin(16.f, 8.f))
					.OnClicked_Lambda([this]() { Offsets.Reset(); return FReply::Handled(); })
					[
						SNew(STextBlock).Text(LOCTEXT("EditReset", "Reset")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
				[
					SNew(SButton).IsFocusable(false).ContentPadding(FMargin(16.f, 8.f))
					.OnClicked_Lambda([this]() { SetEditing(false); return FReply::Handled(); })
					[
						SNew(STextBlock).Text(LOCTEXT("EditDone", "Done")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
					]
				]
			]
		]
		]
	];
	// clang-format on
}

EVisibility SDFTouchControls::PlayButtonsVisibility() const {
	// nothing to fire or jump with in the loadout, and its rows reach under these
	const ADFPlayerController* PC = Controller.Get();
	return PC && PC->IsLoadoutOpen() && !bEditing ? EVisibility::Hidden : EVisibility::SelfHitTestInvisible;
}

void SDFTouchControls::SetEditing(bool bOn) {
	if (bEditing == bOn)
		return;
	bEditing = bOn;
	if (!bOn)
		Save();
}

void SDFTouchControls::MoveButton(FName Button, const FVector2D& Offset) {
	if (Buttons.Contains(Button))
		Offsets.Add(Button, FVector2D(FMath::Clamp(Offset.X, -MaxOffset, MaxOffset), FMath::Clamp(Offset.Y, -MaxOffset, MaxOffset)));
}

void SDFTouchControls::Save() {
	UDFUserSettings& S = UDFUserSettings::Get();
	S.TouchLayout.Reset();
	for (const TPair<FName, FVector2D>& O : Offsets)
		if (!O.Value.IsNearlyZero(0.5))
			S.TouchLayout.Add(O.Key, O.Value); // only what was moved is kept
	S.SaveSettings();
}

TSharedRef<SWidget> SDFTouchControls::Button(FName Sign, FName Action, float Size, const TCHAR* Command) {
	TWeakObjectPtr<ADFPlayerController> PC = Controller;
	const FString Cmd = Command ? Command : TEXT("");
	Buttons.Add(Sign);
	// clang-format off
	return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
		// moved by a render transform: the layout around it stays as it is, touches follow
		.RenderTransform_Lambda([this, Sign]() -> TOptional<FSlateRenderTransform> { return FSlateRenderTransform(FVector2f(Offsets.FindRef(Sign))); })
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SButton)
			.IsFocusable(false) // the game view keeps the keyboard
			.TouchMethod(EButtonTouchMethod::DownAndUp)
			.ButtonColorAndOpacity(FLinearColor(0.1f, 0.2f, 0.6f, 0.5f))
			.HAlign(HAlign_Fill).VAlign(VAlign_Fill)
			.ContentPadding(FMargin(Size * 0.24f))
			.OnPressed_Lambda([this, PC, Action, Cmd]() {
				if (!PC.IsValid() || bEditing)
					return;
				if (!Cmd.IsEmpty())
					PC->ConsoleCommand(Cmd);
				else
					PC->TouchAction(Action, true);
			})
			.OnReleased_Lambda([PC, Action, Cmd]() {
				if (PC.IsValid() && Cmd.IsEmpty())
					PC->TouchAction(Action, false); // also after editing began mid-press: nothing stays held
			})
			[
				SNew(SDFTouchSign).Sign(Sign)
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SDFTouchHandle, [this, Sign](const FVector2D& Delta) { MoveButton(Sign, Offsets.FindRef(Sign) + Delta); })
			.Visibility_Lambda([this]() { return bEditing ? EVisibility::Visible : EVisibility::Collapsed; })
		]
	];
	// clang-format on
}

#undef LOCTEXT_NAMESPACE
