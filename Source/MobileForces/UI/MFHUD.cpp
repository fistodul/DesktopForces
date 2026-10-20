#include "UI/MFHUD.h"
#include "Loadout/MFLoadout.h"
#include "Core/MFGameSettings.h"
#include "Game/MFGameState.h"
#include "Game/MFHoldout.h"
#include "Game/MFPlayerState.h"
#include "Player/MFCharacter.h"
#include "Player/MFPlayerController.h"
#include "Weapons/MFWeapon.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	const FLinearColor White(1.f, 1.f, 1.f);
	const FLinearColor Yellow(1.f, 0.85f, 0.2f);
	const FLinearColor Dim(0.f, 0.f, 0.f, 0.55f);
	const FLinearColor Grey(0.5f, 0.5f, 0.5f);
	const float BlockSizes[5] = { 32.f, 48.f, 64.f, 96.f, 128.f };   // RageHUD HUDSize 0-4
	// MF HUD palette: blue boxes with light borders
	const FLinearColor MFBlue(0.05f, 0.12f, 0.75f, 0.7f);
	const FLinearColor MFBlueDark(0.03f, 0.06f, 0.28f, 0.6f);
	const FLinearColor MFBar(0.1f, 0.25f, 1.f, 0.85f);
	const FLinearColor MFGreyFill(0.12f, 0.12f, 0.12f, 0.5f);
	const FLinearColor MFBorder(0.55f, 0.7f, 1.f, 0.9f);
}

AMFHUD::AMFHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AMFHUD::AddKillMessage(const FString& Killer, const FString& Victim, const FString& Verb)
{
	// RageMessageDeath: red
	AddMessage(Killer.IsEmpty() ? FString::Printf(TEXT("%s %s"), *Victim, *Verb)
		: FString::Printf(TEXT("%s %s %s"), *Killer, *Verb, *Victim), FLinearColor(1.f, 0.f, 0.f));
}

void AMFHUD::AddMessage(const FString& Line, const FLinearColor& Color)
{
	Messages.Add({ Line, Color, GetWorld()->GetTimeSeconds() });
	if (Messages.Num() > 6)   // RageHUD.DrawSmallMessages: 6 lines
	{
		Messages.RemoveAt(0);
	}
}

void AMFHUD::Announce(const FString& InText)
{
	Announcement = InText;
	AnnouncementTime = GetWorld()->GetTimeSeconds();
}

void AMFHUD::NotifyWeaponSelected(const AMFWeapon* Weapon)
{
	SelectedName = Weapon ? Weapon->DisplayName.ToString() : FString();
	SelectedTime = GetWorld()->GetTimeSeconds();
}

void AMFHUD::Text(const FString& S, float X, float Y, const FLinearColor& Color, float Scale, bool bCenter)
{
	if (S.IsEmpty())
	{
		return;
	}
	float W = 0.f, H = 0.f;
	if (bCenter)
	{
		Canvas->TextSize(Font, S, W, H, Scale * UIScale, Scale * UIScale);
	}
	FCanvasTextItem Item(FVector2D(X - W * 0.5f, Y), FText::FromString(S), Font, Color);
	Item.Scale = FVector2D(Scale * UIScale);
	Item.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(Item);
}

UFont* AMFHUD::MFFont(int32 Kind)
{
	static const int32 Sizes[3][5] = { { 7, 11, 14, 21, 28 }, { 9, 14, 18, 27, 36 }, { 12, 18, 24, 36, 48 } };
	const int32 Pt = Sizes[FMath::Clamp(Kind, 0, 2)][FMath::Clamp(SizeIndex, 0, 4)];
	if (const TObjectPtr<UFont>* Found = MFFonts.Find(Pt))
	{
		return *Found;
	}
	UFont* F = LoadObject<UFont>(nullptr, *FString::Printf(TEXT("/Game/MFImported/Fonts/MF%d.MF%d"), Pt, Pt), nullptr, LOAD_NoWarn | LOAD_Quiet);
	MFFonts.Add(Pt, F);
	return F;
}

FVector2D AMFHUD::MFTextSize(const FString& S, int32 Kind)
{
	float W = 0.f, H = 0.f;
	if (UFont* F = MFFont(Kind))
	{
		Canvas->TextSize(F, S, W, H);
	}
	else
	{
		Canvas->TextSize(Font, S, W, H, UIScale, UIScale);
	}
	return FVector2D(W, H);
}

void AMFHUD::MFText(const FString& S, float X, float Y, const FLinearColor& Color, int32 Kind)
{
	UFont* F = MFFont(Kind);
	if (!F)
	{
		Text(S, X, Y, Color, Kind == 2 ? 1.f : 0.8f);
		return;
	}
	FCanvasTextItem Item(FVector2D(FMath::RoundToFloat(X), FMath::RoundToFloat(Y)), FText::FromString(S), F, Color);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void AMFHUD::Box(float X, float Y, float W, float H, const FLinearColor& Color)
{
	FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(W, H), Color);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

float AMFHUD::DrawWeaponIcon(const AMFWeapon* Weapon, float X, float Y, float Height, const FLinearColor& Tint)
{
	if (!Weapon)
	{
		return 0.f;
	}
	TObjectPtr<UTexture2D>& Tex = IconCache.FindOrAdd(Weapon->GetClass());
	if (!Tex && !Weapon->HUDIcon.IsNull())
	{
		Tex = Weapon->HUDIcon.LoadSynchronous();
	}
	if (!Tex)
	{
		Text(Weapon->DisplayName.ToString(), X, Y + Height * 0.3f, Tint, 0.7f);
		return 90.f * UIScale;
	}
	const FIntRect& R = Weapon->HUDIconRect;
	const float TW = Tex->GetSizeX(), TH = Tex->GetSizeY();
	const float W = Height * float(R.Width()) / FMath::Max(1, R.Height());
	FCanvasTileItem Tile(FVector2D(X, Y), Tex->GetResource(), FVector2D(W, Height),
		FVector2D(R.Min.X / TW, R.Min.Y / TH), FVector2D(R.Max.X / TW, R.Max.Y / TH), Tint);
	Tile.BlendMode = SE_BLEND_Masked;
	Canvas->DrawItem(Tile);
	return W;
}

void AMFHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	UIScale = FMath::Max(0.75f, Canvas->ClipY / 720.f);
	if (!Font)
	{
		Font = GEngine->GetMediumFont();
	}
	AMFPlayerController* PC = Cast<AMFPlayerController>(PlayerOwner);
	AMFGameState* GS = GetWorld()->GetGameState<AMFGameState>();
	AMFCharacter* C = PC ? PC->GetMFCharacter() : nullptr;

	// MF block size from HUDSize, smaller while it doesn't fit (RageHUD.SetHUDSize / SetLoadoutSize)
	int32 Size = FMath::Clamp(HUDSize, 0, 4);
	while (Size > 0 && (Canvas->ClipX < BlockSizes[Size] * 11.f || Canvas->ClipY < BlockSizes[Size] * 13.f))
	{
		--Size;
	}
	Block = BlockSizes[Size];
	SizeIndex = Size;
	RS = Block / 64.f;
	const bool bArt = LoadMFArt();

	if (C && C->IsAlive())
	{
		if (C->IsInLoadout())
		{
			bArt ? DrawLoadoutMF(C, PC) : DrawLoadout(C, PC);
		}
		else
		{
			DrawCrosshair(C);
		}
		if (bArt)
		{
			DrawPainMF();
			DrawHealthMF(C);
			DrawInventoryMF(C, PC);
			if (AMFWeapon* W = C->GetCurrentWeapon())
			{
				Text(W->GetHUDStatus(), Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f + 40.f * UIScale, Yellow, 1.2f, true);
			}
		}
		else
		{
			DrawStatus(C);
			DrawWeaponBar(C);
		}
	}
	else if (GS)
	{
		DrawDeadScreen(GS, PC);
	}
	DrawMessages();
	if (PC)
	{
		DrawCommandMenu(PC);
	}
	if (GS)
	{
		DrawGameInfo(GS, PC);
		if ((PC && PC->IsScoreboardShown()) || GS->HasMatchEnded())
		{
			DrawScoreboard(GS);
		}
	}
}

void AMFHUD::DrawCrosshair(AMFCharacter* C)
{
	AMFWeapon* W = C->GetCurrentWeapon();
	if (W && W->DrawHUD(Canvas))
	{
		return;
	}
	const float CX = Canvas->ClipX * 0.5f, CY = Canvas->ClipY * 0.5f;
	if (W && W->IsZoomed() && W->ZoomScale >= 3.f)
	{
		// Scope mask (MF RageWeapon.DrawScopeMask)
		const float R = Canvas->ClipY * 0.45f;
		Box(0, 0, CX - R, Canvas->ClipY, FLinearColor::Black);
		Box(CX + R, 0, Canvas->ClipX - CX - R, Canvas->ClipY, FLinearColor::Black);
		Box(CX - R, 0, 2 * R, CY - R, FLinearColor::Black);
		Box(CX - R, CY + R, 2 * R, Canvas->ClipY - CY - R, FLinearColor::Black);
		Box(CX - R, CY - 0.5f, 2 * R, 1.f, FLinearColor::Black);
		Box(CX - 0.5f, CY - R, 1.f, 2 * R, FLinearColor::Black);
		return;
	}
	if (UTexture2D* X = TexCrosshair[FMath::Clamp(Crosshair, 0, 23)])
	{
		// RageHUD.DrawCrossHair: 32 px at 640 wide; STY_Subtractive GreyColor (only the shape:
		// the texture alpha is its brightness), then the team colour added twice
		const float Size = 32.f * Canvas->ClipX / 640.f;
		const float PX = FMath::RoundToFloat(CX - Size * 0.5f), PY = FMath::RoundToFloat(CY - Size * 0.5f);
		DrawTexRect(X, PX, PY, Size, Size, 1.f, 1.f, 30.f, 30.f, FLinearColor(0.f, 0.f, 0.f, 0.5f), SE_BLEND_Translucent);
		const FLinearColor Team = SetColour(TeamSet());
		DrawTexRect(X, PX, PY, Size, Size, 1.f, 1.f, 30.f, 30.f, Team, SE_BLEND_Additive);
		DrawTexRect(X, PX, PY, Size, Size, 1.f, 1.f, 30.f, 30.f, Team, SE_BLEND_Additive);
		return;
	}
	const float S = 6.f * UIScale, G = 3.f * UIScale;
	const FLinearColor Col(0.f, 1.f, 0.f, 0.85f);
	Box(CX - G - S, CY - 1, S, 2, Col);
	Box(CX + G, CY - 1, S, 2, Col);
	Box(CX - 1, CY - G - S, 2, S, Col);
	Box(CX - 1, CY + G, 2, S, Col);
}

void AMFHUD::Frame(float X, float Y, float W, float H, const FLinearColor& Fill, const FLinearColor& Border)
{
	Box(X, Y, W, H, Fill);
	const float T = FMath::Max(1.f, UIScale);
	Box(X, Y, W, T, Border);
	Box(X, Y + H - T, W, T, Border);
	Box(X, Y, T, H, Border);
	Box(X + W - T, Y, T, H, Border);
}

FString AMFHUD::AmmoText(const AMFWeapon* W, bool bLive)
{
	// MF HUD badge: total / clip size ("210/30"); single-shot items show their count
	if (!W || (W->MaxClipAmmo <= 1 && W->MaxClips <= 1 && !W->bDestroyWhenEmpty))
	{
		return FString();
	}
	if (W->MaxClipAmmo <= 1)
	{
		return FString::FromInt(bLive ? W->GetAmmoCount() : W->MaxClips);
	}
	// RageWeapon.GetAmmoStatus: spare / in the clip
	const int32 Spare = bLive ? W->GetTotalAmmo() : W->MaxClipAmmo * (W->MaxClips - 1);
	return FString::Printf(TEXT("%d/%d"), Spare, bLive ? W->GetClipAmmo() : W->MaxClipAmmo);
}

void AMFHUD::DrawItemCell(const AMFWeapon* W, float X, float Y, float CellW, float CellH, EMFCellState State, const FString& Ammo, const FString& Corner)
{
	// MF inventory box: icon on blue, key number top-left, ammo on a bar underneath
	const float BarH = 13.f * UIScale;
	const FLinearColor Fill = State == EMFCellState::Owned ? MFBlue : State == EMFCellState::Available ? MFBlueDark : MFGreyFill;
	const FLinearColor Border = State == EMFCellState::Unavailable ? Grey : MFBorder;
	Frame(X, Y, CellW, CellH, Fill, Border);
	const FIntRect& R = W->HUDIconRect;
	const float Aspect = float(R.Width()) / FMath::Max(1, R.Height());
	const float IH = FMath::Min(CellH - 4.f * UIScale, (CellW - 6.f * UIScale) / FMath::Max(0.1f, Aspect));
	const float IW = IH * Aspect;
	DrawWeaponIcon(W, X + (CellW - IW) * 0.5f, Y + (CellH - IH) * 0.5f, IH, State == EMFCellState::Unavailable ? Grey : White);
	Text(Corner, X + 3.f * UIScale, Y + 1.f * UIScale, White, 0.55f);
	Frame(X, Y + CellH + 2.f * UIScale, CellW, BarH, State == EMFCellState::Owned ? MFBar : MFGreyFill, Border);
	if (!Ammo.IsEmpty())
	{
		float TW = 0.f, TH = 0.f;
		Canvas->TextSize(Font, Ammo, TW, TH, 0.6f * UIScale, 0.6f * UIScale);
		Text(Ammo, X + CellW - TW - 4.f * UIScale, Y + CellH + 1.f * UIScale, White, 0.6f);
	}
}

void AMFHUD::DrawStatus(AMFCharacter* C)
{
	// Bottom left like MF: health and armour numbers
	const float X = 16.f * UIScale, Y = Canvas->ClipY - 66.f * UIScale;
	const int32 Health = FMath::Max(0, C->GetHealth());
	Frame(X, Y, 120.f * UIScale, 28.f * UIScale, MFBlueDark, MFBorder);
	Text(TEXT("HEALTH"), X + 6.f * UIScale, Y + 7.f * UIScale, White, 0.55f);
	Text(FString::FromInt(Health), X + 64.f * UIScale, Y + 2.f * UIScale, Health > 25 ? White : FLinearColor(1.f, 0.3f, 0.25f), 1.3f);
	if (const int32 Armour = C->GetArmourCharge(); Armour > 0)
	{
		Frame(X, Y + 32.f * UIScale, 120.f * UIScale, 28.f * UIScale, MFBlueDark, MFBorder);
		Text(TEXT("ARMOUR"), X + 6.f * UIScale, Y + 39.f * UIScale, White, 0.55f);
		Text(FString::FromInt(Armour), X + 64.f * UIScale, Y + 34.f * UIScale, FLinearColor(0.5f, 0.7f, 1.f), 1.3f);
	}
	if (AMFWeapon* W = C->GetCurrentWeapon())
	{
		Text(W->GetHUDStatus(), Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f + 40.f * UIScale, Yellow, 1.2f, true);
	}
}

void AMFHUD::DrawWeaponBar(AMFCharacter* C)
{
	// MF carry strip (bottom centre): one box per carried item, as wide as the carry blocks
	// it uses; free blocks stay as empty frames so you see what still fits
	TArray<AMFWeapon*> Items;
	for (AMFWeapon* W : C->GetInventory())
	{
		if (W)
		{
			Items.Add(W);
		}
	}
	Items.StableSort([](const AMFWeapon& A, const AMFWeapon& B) { return A.InventoryGroup < B.InventoryGroup; });
	const float BlockW = 44.f * UIScale, Gap = 3.f * UIScale, CellH = 30.f * UIScale;
	auto WidthOf = [&](int32 Blocks) { return Blocks * BlockW + (Blocks - 1) * Gap; };
	const int32 FreeBlocks = FMath::Max(0, C->MaxCarry - C->GetCurrentCarry());
	float Total = FreeBlocks > 0 ? WidthOf(FreeBlocks) : 0.f;
	for (const AMFWeapon* W : Items)
	{
		Total += WidthOf(FMath::Max(1, W->CarrySize)) + Gap;
	}
	float X = Canvas->ClipX * 0.5f - Total * 0.5f;
	const float Y = Canvas->ClipY - CellH - 22.f * UIScale;
	for (AMFWeapon* W : Items)
	{
		const float CW = WidthOf(FMath::Max(1, W->CarrySize));
		DrawItemCell(W, X, Y, CW, CellH, EMFCellState::Owned, AmmoText(W, true), AMFWeapon::SlotKeyName(W->GetSlotKey()));
		if (W == C->GetCurrentWeapon())
		{
			Frame(X - 2.f, Y - 2.f, CW + 4.f, CellH + 4.f, FLinearColor::Transparent, Yellow);
		}
		X += CW + Gap;
	}
	if (FreeBlocks > 0)
	{
		Frame(X, Y, WidthOf(FreeBlocks), CellH, FLinearColor(0.f, 0.f, 0.f, 0.25f), Grey);
	}
	if (GetWorld()->GetTimeSeconds() - SelectedTime < 1.5f)
	{
		Text(SelectedName, Canvas->ClipX * 0.5f, Y - 26.f * UIScale, Yellow, 1.f, true);
	}
}

void AMFHUD::DrawGameInfo(AMFGameState* GS, AMFPlayerController* PC)
{
	const float CX = Canvas->ClipX * 0.5f;
	if (const int32 Left = GS->GetRemainingTime(); Left >= 0)
	{
		Text(FString::Printf(TEXT("%d:%02d"), Left / 60, Left % 60), CX, 10.f * UIScale, White, 1.2f, true);
	}
	if (GS->IsTeamGame())
	{
		const UMFGameSettings& Settings = UMFGameSettings::Get();
		const float Spacing = 140.f * UIScale;
		float X = CX - Spacing * 0.5f * (GS->NumTeams - 1);
		for (int32 T = 0; T < GS->NumTeams; ++T, X += Spacing)
		{
			Text(FString::Printf(TEXT("%s %d"), *Settings.GetTeamName(T).ToString().ToUpper(), GS->GetTeamScore(T)),
				X, 36.f * UIScale, Settings.GetTeamColor(T), 1.1f, true);
		}
		// Capture points (Holdout / KOH), coloured by the team holding them
		TArray<const AMFHoldoutPoint*> Points;
		for (TActorIterator<AMFHoldoutPoint> It(GetWorld()); It; ++It)
		{
			Points.Add(*It);
		}
		const float PSpacing = 110.f * UIScale;
		float PX = CX - PSpacing * 0.5f * (Points.Num() - 1);
		for (const AMFHoldoutPoint* P : Points)
		{
			const FLinearColor Col = P->OwnerTeam == MF::NoTeam ? FLinearColor(0.6f, 0.6f, 0.6f) : Settings.GetTeamColor(P->OwnerTeam);
			Text(P->PointName, PX, 60.f * UIScale, Col, 0.9f, true);
			PX += PSpacing;
		}
	}
	else if (PC && PC->PlayerState)
	{
		Text(FString::Printf(TEXT("FRAGS %d"), FMath::RoundToInt(PC->PlayerState->GetScore())), 20.f * UIScale, 10.f * UIScale, White, 1.f);
	}
	if (GetWorld()->GetTimeSeconds() - AnnouncementTime < 4.f)
	{
		Text(Announcement, CX, Canvas->ClipY * 0.28f, Yellow, 1.5f, true);
	}
}

void AMFHUD::DrawMessages()
{
	// RageHUD.DrawSmallMessages: top left right of the minimap (3.5 blocks), newest last
	const float Now = GetWorld()->GetTimeSeconds();
	Messages.RemoveAll([Now](const FMessage& M) { return Now - M.Time > 6.f; });
	// PromptAndChatFontSize 3 = GetHUDBigFont; line height from "TEST"
	const float X = Block * 3.5f, LineH = MFTextSize(TEXT("TEST"), 2).Y;
	float Y = 2.f;
	for (const FMessage& M : Messages)
	{
		MFText(M.Text, X, Y, M.Color, 2);
		Y += LineH;
	}
}

void AMFHUD::DrawCommandMenu(AMFPlayerController* PC)
{
	// MF OrdersMainWindow / TargetChildWindow: below the minimap at the left, 32 px rows
	// (on a 1024x768 reference), orders in one column, then who gets it in the next.
	const int32 Stage = PC->GetCommandMenuStage();
	if (Stage == 0)
	{
		return;
	}
	const float Ref = FMath::Min(Canvas->ClipX / 4.f, 256.f) / 256.f;
	const float RowH = 32.f * Ref, ColW = 256.f * Ref, Top = Block * 3.25f;
	const FLinearColor Team = SetColour(TeamSet());
	const FLinearColor Fill(Team.R * 0.25f, Team.G * 0.25f, Team.B * 0.25f, 0.6f);
	auto Column = [&](float X, float Y0, const TArray<FString>& Items, int32 Highlight)
	{
		Frame(X, Y0, ColW, RowH * (Items.Num() + 1), Fill, Team);
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			const float Y = Y0 + RowH * (i + 0.5f);
			const float TY = Y - MFTextSize(TEXT("TEST"), 1).Y * 0.5f + RowH * 0.5f;
			MFText(FString::Printf(TEXT("%d"), (i + 1) % 10), X + 6.f * Ref, TY, FLinearColor(0.6f, 0.7f, 1.f), 0);
			MFText(Items[i], X + 26.f * Ref, TY, i == Highlight ? Yellow : White, 1);
		}
	};
	const TArray<FString> Orders = PC->GetCommandMenuOrders();
	Column(0.f, Top, Orders, Stage == 2 ? PC->GetCommandMenuOrder() : -1);
	if (Stage == 2)
	{
		TArray<FString> Names = { TEXT("All") };
		for (const APlayerState* PS : PC->GetCommandTargets())
		{
			Names.Add(PS->GetPlayerName());
		}
		Column(ColW, Top + RowH * PC->GetCommandMenuOrder(), Names, -1);
	}
}

void AMFHUD::DrawLoadout(AMFCharacter* C, AMFPlayerController* PC)
{
	// MF loadout grid (top left): one row per key, every item of that key in the row.
	// Bright = carried, dark = fits, grey = doesn't fit. Rows with several items are
	// opened with their key; then the item's number (shown in yellow) picks it.
	const TArray<TSubclassOf<AMFWeapon>> Items = PC->GetLoadoutItems();
	const int32 OpenSlot = PC->GetLoadoutOpenSlot();
	const float CellH = 34.f * UIScale, RowH = CellH + 20.f * UIScale, Gap = 4.f * UIScale;
	const float X0 = 10.f * UIScale;
	float Y = 10.f * UIScale, X = X0;
	int32 RowKey = -1, InRow = 0;
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const AMFWeapon* CDO = Items[i]->GetDefaultObject<AMFWeapon>();
		const int32 Key = CDO->GetSlotKey();
		if (Key != RowKey)
		{
			if (RowKey != -1)
			{
				Y += RowH;
			}
			X = X0;
			RowKey = Key;
			InRow = 0;
		}
		++InRow;
		const AMFWeapon* Have = C->FindWeaponOfClass(Items[i]);
		const EMFCellState State = Have ? EMFCellState::Owned : (C->CanCarry(Items[i]) ? EMFCellState::Available : EMFCellState::Unavailable);
		const FIntRect& R = CDO->HUDIconRect;
		const float CellW = FMath::Max(CellH * 1.25f, FMath::Min(CellH * 4.f, CellH * float(R.Width()) / FMath::Max(1, R.Height())));
		DrawItemCell(CDO, X, Y, CellW, CellH, State, AmmoText(Have ? Have : CDO, Have != nullptr), AMFWeapon::SlotKeyName(Key));
		if (Key == OpenSlot)
		{
			Text(FString::FromInt(InRow % 10), X + CellW - 14.f * UIScale, Y, Yellow, 1.1f);
		}
		if (i == PC->LoadoutCursor)
		{
			Frame(X - 2.f, Y - 2.f, CellW + 4.f, CellH + 4.f, FLinearColor::Transparent, Yellow);
		}
		X += CellW + Gap;
	}
	const FString Hint = OpenSlot
		? FString::Printf(TEXT("Row %s: press the item's number"), *AMFWeapon::SlotKeyName(OpenSlot))
		: TEXT("Press Q to exit loadout   -   1-0 / X pick a row, again for an item   -   Up/Down + Fire also work");
	Text(Hint, Canvas->ClipX * 0.5f, Canvas->ClipY * 0.72f, White, 1.f, true);
}

void AMFHUD::DrawScoreboard(AMFGameState* GS)
{
	const TArray<AMFPlayerState*> Players = GS->GetSortedPlayers();
	const UMFGameSettings& Settings = UMFGameSettings::Get();
	const float W = 520.f * UIScale, RowH = 22.f * UIScale;
	const float X = Canvas->ClipX * 0.5f - W * 0.5f;
	float Y = 90.f * UIScale;
	Box(X - 10, Y - 10, W + 20, (Players.Num() + 2) * RowH + 20.f * UIScale, FLinearColor(0.f, 0.f, 0.f, 0.7f));
	Text(GS->HasMatchEnded() ? GS->WinnerText : GS->GameName, Canvas->ClipX * 0.5f, Y, Yellow, 1.2f, true);
	Y += RowH * 1.5f;
	Text(TEXT("PLAYER"), X, Y, Yellow, 0.8f);
	Text(TEXT("FRAGS"), X + W * 0.6f, Y, Yellow, 0.8f);
	Text(TEXT("DEATHS"), X + W * 0.75f, Y, Yellow, 0.8f);
	Text(TEXT("PING"), X + W * 0.9f, Y, Yellow, 0.8f);
	Y += RowH;
	for (const AMFPlayerState* PS : Players)
	{
		const FLinearColor Col = PS->HasTeam() ? Settings.GetTeamColor(PS->GetTeam()) : White;
		Text(PS->GetPlayerName(), X, Y, Col, 0.9f);
		Text(FString::FromInt(FMath::RoundToInt(PS->GetScore())), X + W * 0.6f, Y, Col, 0.9f);
		Text(FString::FromInt(PS->Deaths), X + W * 0.75f, Y, Col, 0.9f);
		Text(PS->IsABot() ? TEXT("BOT") : FString::FromInt(PS->GetPingInMilliseconds()), X + W * 0.9f, Y, Col, 0.9f);
		Y += RowH;
	}
}

void AMFHUD::DrawDeadScreen(AMFGameState* GS, AMFPlayerController* PC)
{
	if (GS->HasMatchEnded() || !PC)
	{
		return;
	}
	const AMFPlayerState* PS = PC->GetPlayerState<AMFPlayerState>();
	const float Left = PS ? PS->DeathTime + GS->RespawnWait - GS->GetServerWorldTimeSeconds() : 0.f;
	Text(Left > 0.f ? FString::Printf(TEXT("Respawning in %d"), FMath::CeilToInt(Left)) : TEXT("Respawning..."),
		Canvas->ClipX * 0.5f, Canvas->ClipY * 0.6f, White, 1.2f, true);
}

// --- MF art --------------------------------------------------------------------------
// Port of RageHUD (Rage.u): TexRects and layout from its defaultproperties / Draw* code.
// Translucent (UE1 STY_Translucent) = additive, tinted by the colour set.

namespace
{
	// RageHUD.Colour_Sets: blue, red, white, grey
	const FLinearColor ColourSets[4] = {
		FLinearColor(0.f, 0.f, 1.f), FLinearColor(1.f, 0.f, 0.f), FLinearColor(1.f, 1.f, 1.f),
		FLinearColor(50.f / 255.f, 50.f / 255.f, 50.f / 255.f) };
	// Box_Sets_* rows in Rage.Boxes per set (Y): blue 128, red 64, white 0, grey 192
	const float BoxRowY[4] = { 128.f, 64.f, 0.f, 192.f };

	UTexture2D* LoadTex(const TCHAR* Path)
	{
		return LoadObject<UTexture2D>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

bool AMFHUD::LoadMFArt()
{
	if (!bTriedArt)
	{
		bTriedArt = true;
		TexBoxes = LoadTex(TEXT("/Game/MFImported/Textures/Rage/Boxes.Boxes"));
		TexBacks = LoadTex(TEXT("/Game/MFImported/Textures/Rage/Backs.Backs"));
		TexIcons = LoadTex(TEXT("/Game/MFImported/Textures/Rage/Icons.Icons"));
		TexLoadoutBack[0] = LoadTex(TEXT("/Game/MFImported/Textures/Rage/LoadoutBackBlue.LoadoutBackBlue"));
		TexLoadoutBack[1] = LoadTex(TEXT("/Game/MFImported/Textures/Rage/LoadoutBackRed.LoadoutBackRed"));
		TexPain[0] = LoadTex(TEXT("/Game/MFImported/Textures/Rage/OwBlue.OwBlue"));
		TexPain[1] = LoadTex(TEXT("/Game/MFImported/Textures/Rage/OwRed.OwRed"));
		for (int32 i = 0; i < 24; ++i)
		{
			const FString Name = FString::Printf(TEXT("CrossHair%c"), TCHAR('A' + i));
			TexCrosshair[i] = LoadTex(*FString::Printf(TEXT("/Game/MFImported/Textures/Rage/%s.%s"), *Name, *Name));
		}
	}
	return TexBoxes && TexBacks && TexIcons;
}

void AMFHUD::NotifyPain(const FVector& FromDir)
{
	HitTime = GetWorld()->GetTimeSeconds();
	HitDir = FromDir;
}

void AMFHUD::DrawPainMF()
{
	// RageHUD.DrawPainIndicators: the screen edge facing the attacker glows in the team's
	// colour for PainFadeTime (1 s); undirected damage lights all four edges
	const float Intensity = 1.f - (GetWorld()->GetTimeSeconds() - HitTime) / 1.f;
	UTexture2D* Tex = TexPain[TeamSet()];
	if (Intensity <= 0.f || !Tex || !PlayerOwner)
	{
		return;
	}
	bool Side[4] = { true, true, true, true };   // left, right, top, bottom
	if (!HitDir.IsNearlyZero())
	{
		const float Rel = FMath::FindDeltaAngleDegrees(PlayerOwner->GetControlRotation().Yaw, HitDir.Rotation().Yaw);
		Side[0] = Rel < -45.f && Rel >= -135.f;
		Side[1] = Rel > 45.f && Rel <= 135.f;
		Side[2] = FMath::Abs(Rel) <= 45.f;
		Side[3] = FMath::Abs(Rel) > 135.f;
	}
	const FLinearColor C = FLinearColor::White * Intensity;
	const float SV = Canvas->ClipY / 512.f, SH = Canvas->ClipX / 512.f;
	const float CX = Canvas->ClipX * 0.5f, CY = Canvas->ClipY * 0.5f;
	const float VW = 255.f * SV * 0.5f, VH = 255.f * SV;   // left/right pieces
	const float HW = 255.f * SH, HH = 255.f * SH * 0.5f;   // top/bottom pieces
	// quarter texture mirrored into place: (U, V, UL, VL) with negative extents flip
	auto Piece = [&](float X, float Y, float W, float H, bool bFlipU, bool bFlipV)
	{
		DrawTexRect(Tex, X, Y, W, H, bFlipU ? 255.f : 1.f, bFlipV ? 255.f : 1.f, bFlipU ? -254.f : 254.f, bFlipV ? -254.f : 254.f,
			C, SE_BLEND_Additive);
	};
	if (Side[0]) { Piece(0.f, CY - VH, VW, VH, false, false); Piece(0.f, CY, VW, VH, false, true); }
	if (Side[1]) { Piece(Canvas->ClipX - VW, CY - VH, VW, VH, true, false); Piece(Canvas->ClipX - VW, CY, VW, VH, true, true); }
	if (Side[2]) { Piece(CX - HW, 0.f, HW, HH, true, true); Piece(CX, 0.f, HW, HH, false, true); }
	if (Side[3]) { Piece(CX - HW, Canvas->ClipY - HH, HW, HH, true, false); Piece(CX, Canvas->ClipY - HH, HW, HH, false, false); }
}

void AMFHUD::ChangeCrosshair(int32 Delta)
{
	Crosshair = (Crosshair + Delta + 24) % 24;
	SaveConfig();
}

void AMFHUD::ChangeHUDSize(int32 Delta)
{
	HUDSize = FMath::Clamp(HUDSize + Delta, 0, 4);
	SaveConfig();
}

void AMFHUD::DrawTexRect(UTexture2D* Tex, float X, float Y, float W, float H, float U, float V, float UL, float VL,
	const FLinearColor& Color, ESimpleElementBlendMode Blend)
{
	if (!Tex || W <= 0.f || H <= 0.f)
	{
		return;
	}
	const float TW = Tex->GetSizeX(), TH = Tex->GetSizeY();
	FCanvasTileItem Tile(FVector2D(X, Y), Tex->GetResource(), FVector2D(W, H),
		FVector2D(U / TW, V / TH), FVector2D((U + UL) / TW, (V + VL) / TH), Color);
	Tile.BlendMode = Blend;
	Canvas->DrawItem(Tile);
}

int32 AMFHUD::TeamSet() const
{
	// RageHUD.TeamIndex: red team -> 1, everyone else blue
	const APlayerController* PC = PlayerOwner;
	const AMFPlayerState* PS = PC ? PC->GetPlayerState<AMFPlayerState>() : nullptr;
	return PS && PS->GetTeam() == 1 ? 1 : 0;
}

FLinearColor AMFHUD::SetColour(int32 Set) const
{
	// Teams beyond MF's two keep their configured colour for fills and bars
	if (Set <= 1)
	{
		const APlayerController* PC = PlayerOwner;
		const AMFPlayerState* PS = PC ? PC->GetPlayerState<AMFPlayerState>() : nullptr;
		if (PS && PS->HasTeam() && PS->GetTeam() > 1)
		{
			return UMFGameSettings::Get().GetTeamColor(PS->GetTeam());
		}
	}
	return ColourSets[FMath::Clamp(Set, 0, 3)];
}

void AMFHUD::DrawWeapIconMF(const AMFWeapon* W, float X, float Y, int32 Set, const FString& Ammo, const FString& Corner)
{
	// RageHUD.DrawWeapIcon: box (single or left/middle.../right), translucent back in the
	// set colour, ammo bar along the bottom quarter, ammo text, group key, icon centred
	const int32 Count = FMath::Max(1, W->CarrySize);
	const int32 MaxAmmo = FMath::Max(1, W->MaxClips * W->MaxClipAmmo);
	float AmmoW = 64.f * Count * FMath::Clamp(float(W->GetAmmoCount()) / MaxAmmo, 0.f, 1.f);
	if (!W->GetOwner())
	{
		AmmoW = 64.f * Count;   // store item: full
	}
	const FLinearColor Back = SetColour(Set);
	const FLinearColor Bar = SetColour(Set == 2 ? TeamSet() : Set);   // RageHUD.SetAmmoBarColour
	for (int32 i = 0; i < Count; ++i)
	{
		// column in the atlases: 0 left, 64 middle, 128 right, 192 single
		const float Col = Count == 1 ? 192.f : (i == 0 ? 0.f : (i == Count - 1 ? 128.f : 64.f));
		const float BX = X + i * Block;
		// ammo filling from the right: this block's share
		const float Seg = FMath::Clamp(AmmoW - (Count - 1 - i) * 64.f, 0.f, 64.f);
		if (Seg > 0.f)
		{
			DrawTexRect(TexBacks, BX + Block - Seg * RS, Y + Block * 0.75f, Seg * RS, Block * 0.25f,
				Col + 64.f - Seg, 48.f, Seg, 16.f, Bar, SE_BLEND_Additive);
		}
		DrawTexRect(TexBacks, BX, Y, Block, Block * 0.75f, Col, 0.f, 64.f, 48.f, Back, SE_BLEND_Additive);
		DrawTexRect(TexBoxes, BX, Y, Block, Block, Col, BoxRowY[FMath::Clamp(Set, 0, 3)], 64.f, 64.f,
			FLinearColor::White, SE_BLEND_Translucent);
	}
	// weapon picture: native width x RenderScale, block high, centred (grey when no room)
	TObjectPtr<UTexture2D>& Icon = IconCache.FindOrAdd(W->GetClass());
	if (!Icon && !W->HUDIcon.IsNull())
	{
		Icon = W->HUDIcon.LoadSynchronous();
	}
	if (Icon)
	{
		const FIntRect& R = W->HUDIconRect;
		const float IW = R.Width() * RS * Block / FMath::Max(1.f, R.Height() * RS);
		DrawTexRect(Icon, X + Count * Block * 0.5f - IW * 0.5f, Y, IW, Block, R.Min.X, R.Min.Y, R.Width(), R.Height(),
			Set == 3 ? ColourSets[3] * 3.f : FLinearColor::White, SE_BLEND_Translucent);
	}
	// ammo bottom right (small font), key top left in a pale team colour
	if (!Ammo.IsEmpty())
	{
		float TW = 0.f, TH = 0.f;
		Canvas->TextSize(GEngine->GetSmallFont(), Ammo, TW, TH, RS, RS);
		FCanvasTextItem T(FVector2D(X + Count * Block - TW - 8.f * RS, Y + Block - TH - 2.f * RS), FText::FromString(Ammo),
			GEngine->GetSmallFont(), FLinearColor::White);
		T.Scale = FVector2D(RS);
		Canvas->DrawItem(T);
	}
	if (Set != 3 && !Corner.IsEmpty())
	{
		const FLinearColor Key = TeamSet() == 1 ? FLinearColor(1.f, 0.75f, 0.75f) : FLinearColor(0.75f, 0.75f, 1.f);
		float TW = 0.f, TH = 0.f;
		Canvas->TextSize(GEngine->GetSmallFont(), Corner, TW, TH, RS, RS);
		FCanvasTextItem T(FVector2D(X + (20.f * RS - TW) * 0.5f, Y + (26.f * RS - TH) * 0.5f), FText::FromString(Corner),
			GEngine->GetSmallFont(), Key);
		T.Scale = FVector2D(RS);
		Canvas->DrawItem(T);
	}
}

void AMFHUD::DrawEmptyMF(int32 Blocks, float X, float Y)
{
	// RageHUD.DrawEmptyIcon: grey translucent back + grey frame per free carry block
	for (int32 i = 0; i < Blocks; ++i)
	{
		const float Col = Blocks == 1 ? 192.f : (i == 0 ? 0.f : (i == Blocks - 1 ? 128.f : 64.f));
		DrawTexRect(TexBacks, X + i * Block, Y, Block, Block * 0.75f, Col, 0.f, 64.f, 48.f, ColourSets[3], SE_BLEND_Additive);
		DrawTexRect(TexBoxes, X + i * Block, Y, Block, Block, Col, 192.f, 64.f, 64.f, FLinearColor::White, SE_BLEND_Translucent);
	}
}

void AMFHUD::DrawInventoryMF(AMFCharacter* C, AMFPlayerController* PC)
{
	// RageHUD.DrawInventory at (ClipX/2 - 3.5 blocks, bottom): carried items, knife one block
	TArray<AMFWeapon*> Items;
	for (AMFWeapon* W : C->GetInventory())
	{
		if (W)
		{
			Items.Add(W);
		}
	}
	Items.StableSort([](const AMFWeapon& A, const AMFWeapon& B) { return A.InventoryGroup < B.InventoryGroup; });
	const bool bLoadout = C->IsInLoadout();
	// selected: the loadout cursor in the loadout; in play the weapon in hand (or coming up) -
	// MF showed that with the first-person model, which we don't have yet
	const AMFWeapon* Selected = bLoadout ? PC->GetLoadoutCursorWeapon()
		: (C->GetPendingWeapon() ? C->GetPendingWeapon() : C->GetCurrentWeapon());
	float X = Canvas->ClipX * 0.5f - Block * 3.5f;
	const float Y = Canvas->ClipY - Block;
	int32 Left = C->MaxCarry;
	for (AMFWeapon* W : Items)
	{
		DrawWeapIconMF(W, X, Y, W == Selected ? 2 : TeamSet(), AmmoText(W, true), AMFWeapon::SlotKeyName(W->GetSlotKey()));
		X += FMath::Max(1, W->CarrySize) * Block;
		Left -= W->CarrySize;
	}
	if (Left > 0)
	{
		DrawEmptyMF(Left, X, Y);
	}
	if (!bLoadout && GetWorld()->GetTimeSeconds() - SelectedTime < 1.5f)
	{
		Text(SelectedName, Canvas->ClipX * 0.5f, Y - 28.f * RS, Yellow, RS, true);
	}
}

void AMFHUD::DrawHealthMF(AMFCharacter* C)
{
	const int32 T = TeamSet();
	const FLinearColor Fill = SetColour(T);
	// Health: soldier silhouette filled from the bottom (RageHUD.DrawHealth)
	const float HX = 0.f, HY = Canvas->ClipY - 128.f * RS;
	const int32 Health = FMath::Clamp(C->GetHealth(), 0, 100);
	const float Lvl = 128.f * Health / 100.f;
	DrawTexRect(TexBacks, HX, HY + (128.f - Lvl) * RS, 64.f * RS, Lvl * RS, 0.f, 64.f + 128.f - Lvl, 64.f, Lvl, Fill, SE_BLEND_Additive);
	DrawTexRect(TexIcons, HX, HY, 64.f * RS, 128.f * RS, T == 1 ? 128.f : 192.f, 0.f, 64.f, 128.f, FLinearColor::White, SE_BLEND_Translucent);
	// RageHUD.DrawHealth: GetHUDMedFont, at ((64 + 8) * RS / 2 - W * 0.75, 48 * RS - H / 2)
	const FString HS = FString::FromInt(Health);
	FVector2D TS = MFTextSize(HS, 1);
	MFText(HS, HX + 72.f * RS * 0.5f - TS.X * 0.75f, HY + 48.f * RS - TS.Y * 0.5f, FLinearColor::White, 1);
	// Armour: vest next to it, only while carrying armour (RageHUD.DrawArmour)
	if (const int32 Armour = C->GetArmourCharge(); Armour > 0)
	{
		const float AX = 46.f * RS, AY = Canvas->ClipY - 56.f * RS;
		const float AL = 56.f * FMath::Min(Armour, 100) / 100.f;
		DrawTexRect(TexBacks, AX, AY + (56.f - AL) * RS, 68.f * RS, AL * RS, 0.f, 192.f + 56.f - AL, 64.f, AL, Fill, SE_BLEND_Additive);
		DrawTexRect(TexIcons, AX, AY, 64.f * RS, 56.f * RS, T == 1 ? 128.f : 192.f, 128.f, 64.f, 56.f, FLinearColor::White, SE_BLEND_Translucent);
		const FString AS = FString::FromInt(Armour);
		TS = MFTextSize(AS, 1);
		MFText(AS, AX + 32.f * RS - TS.X * 0.75f, AY + 28.f * RS - TS.Y * 0.5f, FLinearColor::White, 1);
	}
	// Loadout available here (RageHUD: CanDoLoadout -> Loadout_Back + Loadout_Team)
	if (!C->IsInLoadout() && UMFLoadoutLibrary::FindLoadoutSourceFor(C))
	{
		const float LX = 64.f * RS, LY = Canvas->ClipY - Block * 2.f;
		DrawTexRect(TexBacks, LX, LY, 64.f * RS, 32.f * RS, 64.f, 96.f, 64.f, 32.f, Fill, SE_BLEND_Additive);
		DrawTexRect(TexIcons, LX, LY, 64.f * RS, 32.f * RS, T == 1 ? 0.f : 64.f, 160.f, 64.f, 32.f, FLinearColor::White, SE_BLEND_Translucent);
	}
}

void AMFHUD::DrawLoadoutMF(AMFCharacter* C, AMFPlayerController* PC)
{
	// Backdrop: the team's emblem in the middle (RageHUD.DrawLoadoutBackdrop, 510 at 800x600)
	if (UTexture2D* Back = TexLoadoutBack[TeamSet()])
	{
		const float MW = 510.f * Canvas->ClipX / 800.f, MH = 510.f * Canvas->ClipY / 600.f;
		DrawTexRect(Back, (Canvas->ClipX - MW) * 0.5f, (Canvas->ClipY - MH) * 0.5f, MW, MH, 1.f, 1.f, 255.f, 255.f,
			FLinearColor::White, SE_BLEND_Additive);
	}
	// Store rows from the top left: one row per key, items side by side (RageHUD.DrawLoadout)
	const TArray<TSubclassOf<AMFWeapon>> Items = PC->GetLoadoutItems();
	const int32 OpenSlot = PC->GetLoadoutOpenSlot();
	float Y = 16.f - Block, X = 0.f;
	int32 RowKey = -1, InRow = 0;
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const AMFWeapon* CDO = Items[i]->GetDefaultObject<AMFWeapon>();
		const int32 Key = CDO->GetSlotKey();
		if (Key != RowKey)
		{
			Y += Block;
			X = 0.f;
			RowKey = Key;
			InRow = 0;
		}
		++InRow;
		const float W = FMath::Max(1, CDO->CarrySize) * Block;
		if (X > 0.f && X + W > Canvas->ClipX)
		{
			X = 0.f;            // wrap like MF when a row doesn't fit
			Y += Block;
		}
		const int32 Set = i == PC->LoadoutCursor ? 2 : (!C->CanCarry(Items[i]) ? 3 : TeamSet());
		DrawWeapIconMF(CDO, X, Y, Set, AmmoText(CDO, false), AMFWeapon::SlotKeyName(Key));
		if (Key == OpenSlot)
		{
			Text(FString::FromInt(InRow % 10), X + W - 16.f * RS, Y + 2.f, Yellow, RS);
		}
		X += W;
	}
	const FString Msg = OpenSlot
		? FString::Printf(TEXT("Row %s: press the item's number"), *AMFWeapon::SlotKeyName(OpenSlot))
		: TEXT("Press Q to Exit Loadout");
	Text(Msg, Canvas->ClipX * 0.5f, Canvas->ClipY - Block * 4.f, White, 1.4f * RS, true);
}
