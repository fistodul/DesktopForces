#include "UI/MFHUD.h"
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
	KillFeed.Add({ Killer, Victim, Verb, GetWorld()->GetTimeSeconds() });
	if (KillFeed.Num() > 4)
	{
		KillFeed.RemoveAt(0);
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

	if (C && C->IsAlive())
	{
		if (C->IsInLoadout())
		{
			DrawLoadout(C, PC);
		}
		else
		{
			DrawCrosshair(C);
		}
		DrawStatus(C);
		DrawWeaponBar(C);
	}
	else if (GS)
	{
		DrawDeadScreen(GS, PC);
	}
	DrawKillFeed();
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
	const int32 Total = bLive ? W->GetAmmoCount() : W->MaxClipAmmo * W->MaxClips;
	return FString::Printf(TEXT("%d/%d"), Total, bLive ? W->GetClipAmmo() : W->MaxClipAmmo);
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
	const float Block = 44.f * UIScale, Gap = 3.f * UIScale, CellH = 30.f * UIScale;
	auto WidthOf = [&](int32 Blocks) { return Blocks * Block + (Blocks - 1) * Gap; };
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

void AMFHUD::DrawKillFeed()
{
	const float Now = GetWorld()->GetTimeSeconds();
	float Y = 40.f * UIScale;
	for (const FKill& M : KillFeed)
	{
		if (Now - M.Time > 6.f)
		{
			continue;
		}
		const FString Line = M.Killer.IsEmpty() ? FString::Printf(TEXT("%s %s"), *M.Victim, *M.Verb)
			: FString::Printf(TEXT("%s %s %s"), *M.Killer, *M.Verb, *M.Victim);
		Text(Line, Canvas->ClipX - 320.f * UIScale, Y, White, 0.8f);
		Y += 18.f * UIScale;
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
