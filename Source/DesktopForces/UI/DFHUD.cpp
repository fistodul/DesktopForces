#include "UI/DFHUD.h"
#include "UI/DFCrosshairs.h"
#include "Core/DFContent.h"
#include "Core/DFUserSettings.h"
#include "World/DFMapActors.h"
#include "Game/CTF/Flag.h"
#include "Game/Safecracker/Safecracker.h"
#include "Loadout/DFLoadout.h"
#include "Core/DFGameSettings.h"
#include "Game/DFGameState.h"
#include "Game/Holdout/Holdout.h"
#include "Game/DFPlayerState.h"
#include "Player/DFCharacter.h"
#include "Player/DFPlayerController.h"
#include "Weapons/DFWeapon.h"
#include "Vehicles/DFVehicles.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace {
	const FLinearColor White(1.f, 1.f, 1.f);
	const FLinearColor Yellow(1.f, 0.85f, 0.2f);
	const FLinearColor Dim(0.f, 0.f, 0.f, 0.55f);
	const FLinearColor Grey(0.5f, 0.5f, 0.5f);
	const float BlockSizes[5] = { 32.f, 48.f, 64.f, 96.f, 128.f }; // RageHUD HUDSize 0-4
	// MF HUD palette: blue boxes with light borders
	const FLinearColor DFBlue(0.05f, 0.12f, 0.75f, 0.7f);
	const FLinearColor DFBlueDark(0.03f, 0.06f, 0.28f, 0.6f);
	const FLinearColor DFBar(0.1f, 0.25f, 1.f, 0.85f);
	const FLinearColor DFGreyFill(0.12f, 0.12f, 0.12f, 0.5f);
	const FLinearColor DFBorder(0.55f, 0.7f, 1.f, 0.9f);
} // namespace

ADFHUD::ADFHUD() {
	PrimaryActorTick.bCanEverTick = false;
}

void ADFHUD::AddKillMessage(const FString& Killer, const FString& Victim, const FString& Verb) {
	// RageMessageDeath: red
	AddMessage(Killer.IsEmpty() ? FString::Printf(TEXT("%s %s"), *Victim, *Verb)
								: FString::Printf(TEXT("%s %s %s"), *Killer, *Verb, *Victim),
		FLinearColor(1.f, 0.f, 0.f));
}

void ADFHUD::AddMessage(const FString& Line, const FLinearColor& Color) {
	Messages.Add({ Line, Color, float(GetWorld()->GetTimeSeconds()) });
	if (Messages.Num() > 6) // RageHUD.DrawSmallMessages: 6 lines
		Messages.RemoveAt(0);
}

void ADFHUD::Announce(const FString& InText) {
	Announcement = InText;
	AnnouncementTime = GetWorld()->GetTimeSeconds();
}

void ADFHUD::NotifyWeaponSelected(const ADFWeapon* Weapon) {
	SelectedName = Weapon ? Weapon->DisplayName.ToString() : FString();
	SelectedTime = GetWorld()->GetTimeSeconds();
}

void ADFHUD::Text(const FString& S, float X, float Y, const FLinearColor& Color, float Scale, bool bCenter) {
	if (S.IsEmpty())
		return;
	float W = 0.f, H = 0.f;
	if (bCenter)
		Canvas->TextSize(Font, S, W, H, Scale * UIScale, Scale * UIScale);
	FCanvasTextItem Item(FVector2D(X - W * 0.5f, Y), FText::FromString(S), Font, Color);
	Item.Scale = FVector2D(Scale * UIScale);
	Item.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(Item);
}

UFont* ADFHUD::DFFont(int32 Kind) {
	static const int32 Sizes[3][5] = { { 7, 11, 14, 21, 28 }, { 9, 14, 18, 27, 36 }, { 12, 18, 24, 36, 48 } };
	const int32 Pt = Sizes[FMath::Clamp(Kind, 0, 2)][FMath::Clamp(SizeIndex, 0, 4)];
	if (const TObjectPtr<UFont>* Found = DFFonts.Find(Pt))
		return *Found;
	UFont* F = LoadObject<UFont>(nullptr, *FString::Printf(TEXT("/MobileForcesClassic/Fonts/MF%d.MF%d"), Pt, Pt), nullptr, LOAD_NoWarn | LOAD_Quiet);
	DFFonts.Add(Pt, F);
	return F;
}

FVector2D ADFHUD::DFTextSize(const FString& S, int32 Kind) {
	float W = 0.f, H = 0.f;
	if (UFont* F = DFFont(Kind))
		Canvas->TextSize(F, S, W, H);
	else
		Canvas->TextSize(Font, S, W, H, UIScale, UIScale);
	return FVector2D(W, H);
}

void ADFHUD::DFText(const FString& S, float X, float Y, const FLinearColor& Color, int32 Kind) {
	UFont* F = DFFont(Kind);
	if (!F) {
		Text(S, X, Y, Color, Kind == 2 ? 1.f : 0.8f);
		return;
	}
	FCanvasTextItem Item(FVector2D(FMath::RoundToFloat(X), FMath::RoundToFloat(Y)), FText::FromString(S), F, Color);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void ADFHUD::Box(float X, float Y, float W, float H, const FLinearColor& Color) {
	FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(W, H), Color);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

float ADFHUD::DrawWeaponIcon(const ADFWeapon* Weapon, float X, float Y, float Height, const FLinearColor& Tint) {
	if (!Weapon)
		return 0.f;
	TObjectPtr<UTexture2D>& Tex = IconCache.FindOrAdd(Weapon->GetClass());
	if (!Tex && !Weapon->HUDIcon.IsEmpty())
		Tex = DF::LoadClassic<UTexture2D>(Weapon->HUDIcon);
	if (!Tex) {
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

void ADFHUD::DrawHUD() {
	Super::DrawHUD();
	if (!Canvas)
		return;
	UIScale = FMath::Max(0.75f, Canvas->ClipY / 720.f);
	if (!Font)
		Font = GEngine->GetMediumFont();
	ADFPlayerController* PC = Cast<ADFPlayerController>(PlayerOwner);
	ADFGameState* GS = GetWorld()->GetGameState<ADFGameState>();
	ADFCharacter* C = PC ? PC->GetMFCharacter() : nullptr;

	// MF block size from HUDSize, smaller while it doesn't fit (RageHUD.SetHUDSize / SetLoadoutSize)
	int32 Size = FMath::Clamp(UDFUserSettings::Get().HUDSize, 0, 4);
	while (Size > 0 && (Canvas->ClipX < BlockSizes[Size] * 11.f || Canvas->ClipY < BlockSizes[Size] * 13.f))
		--Size;
	Block = BlockSizes[Size];
	SizeIndex = Size;
	RS = Block / 64.f;
	const bool bArt = LoadMFArt();

	if (C && C->IsAlive()) {
		if (C->IsInLoadout())
			bArt ? DrawLoadoutMF(C, PC) : DrawLoadout(C, PC);
		else
			DrawCrosshair(C);
		if (bArt) {
			DrawPainMF();
			if (!C->IsInLoadout() && !(PC && PC->IsScoreboardShown()) && !(GS && GS->HasMatchEnded()))
				DrawMiniMapMF(C); // the loadout grid and the scoreboard take its place in MF
			DrawHealthMF(C);
			DrawInventoryMF(C, PC);
			if (ADFWeapon* W = C->GetCurrentWeapon())
				Text(W->GetHUDStatus(), Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f + 40.f * UIScale, Yellow, 1.2f, true);
		} else {
			DrawStatus(C);
			DrawWeaponBar(C);
		}
	} else if (const ADFVehicle* V = PC ? Cast<ADFVehicle>(PC->GetPawn()) : nullptr) {
		// driving: vehicle health and speed bottom centre, the minimap stays
		if (ADFCharacter* Rider = V->GetDriver(); Rider && bArt && !(PC->IsScoreboardShown()) && !(GS && GS->HasMatchEnded())) {
			DrawMiniMapMF(Rider);
			DrawHealthMF(Rider);
		}
		DFText(FString::Printf(TEXT("%s   %d%%   %d km/h"), *V->DisplayName.ToString(), FMath::Max(0, V->Health), FMath::RoundToInt(V->GetSpeedKmh())),
			Canvas->ClipX * 0.5f - DFTextSize(TEXT("Buggy   100%   100 km/h"), 1).X * 0.5f, Canvas->ClipY - Block * 0.9f, White, 1);
	} else if (GS) {
		DrawDeadScreen(GS, PC);
	}
	DrawMessages();
	if (GS && !GS->VoteText.IsEmpty()) {
		// voteHUDrender: top left under the minimap
		const float X = 8.f * UIScale, Y0 = Block * 3.4f, LineH = DFTextSize(TEXT("TEST"), 0).Y * 1.2f;
		const int32 Left = FMath::Max(0, FMath::CeilToInt(GS->VoteEndTime - GS->GetServerWorldTimeSeconds()));
		const FString Lines[] = { TEXT("Voting in progress"), GS->VoteText, FString::Printf(TEXT("Upvotes: %d (vote yes)"), GS->VoteYes),
			FString::Printf(TEXT("Downvotes: %d (vote no)"), GS->VoteNo), FString::Printf(TEXT("%d s left"), Left) };
		for (int32 I = 0; I < UE_ARRAY_COUNT(Lines); ++I)
			DFText(Lines[I], X, Y0 + I * LineH, I == 0 ? Yellow : White, 0);
	}
	if (PC) {
		DrawCommandMenu(PC);
		DrawCaptainMarkers(PC);
	}
	if (GS) {
		DrawGameInfo(GS, PC);
		if ((PC && PC->IsScoreboardShown()) || GS->HasMatchEnded())
			DrawScoreboard(GS);
	}
}

void ADFHUD::DrawCrosshair(ADFCharacter* C) {
	ADFWeapon* W = C->GetCurrentWeapon();
	if (W && W->DrawHUD(Canvas))
		return;
	const float CX = Canvas->ClipX * 0.5f, CY = Canvas->ClipY * 0.5f;
	if (W && W->IsZoomed() && W->ZoomScale >= 3.f) {
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
	if (UTexture2D* X = TexCrosshair[FMath::Clamp(UDFUserSettings::Get().Crosshair, 0, 23)]) {
		// RageHUD.DrawCrossHair: 32 px at 640 wide; STY_Subtractive GreyColor (only the shape:
		// the texture alpha is its brightness), then the team colour added twice
		const float Size = 32.f * Canvas->ClipX / 640.f * FMath::Clamp(UDFUserSettings::Get().CrosshairSize, 0.25f, 2.f);
		const FIntRect UV = DF::CrosshairBounds(UDFUserSettings::Get().Crosshair);
		const float PX = FMath::RoundToFloat(CX - Size * 0.5f), PY = FMath::RoundToFloat(CY - Size * 0.5f);
		DrawTexRect(X, PX, PY, Size, Size, UV.Min.X, UV.Min.Y, UV.Width(), UV.Height(), FLinearColor(0.f, 0.f, 0.f, 0.5f), SE_BLEND_Translucent);
		const FLinearColor Team = SetColour(TeamSet());
		DrawTexRect(X, PX, PY, Size, Size, UV.Min.X, UV.Min.Y, UV.Width(), UV.Height(), Team, SE_BLEND_Additive);
		DrawTexRect(X, PX, PY, Size, Size, UV.Min.X, UV.Min.Y, UV.Width(), UV.Height(), Team, SE_BLEND_Additive);
		return;
	}
	const float Scale = FMath::Clamp(UDFUserSettings::Get().CrosshairSize, 0.25f, 2.f);
	const float S = 6.f * UIScale * Scale, G = 3.f * UIScale * Scale;
	const FLinearColor Col(0.f, 1.f, 0.f, 0.85f);
	Box(CX - G - S, CY - 1, S, 2, Col);
	Box(CX + G, CY - 1, S, 2, Col);
	Box(CX - 1, CY - G - S, 2, S, Col);
	Box(CX - 1, CY + G, 2, S, Col);
}

void ADFHUD::Frame(float X, float Y, float W, float H, const FLinearColor& Fill, const FLinearColor& Border) {
	Box(X, Y, W, H, Fill);
	const float T = FMath::Max(1.f, UIScale);
	Box(X, Y, W, T, Border);
	Box(X, Y + H - T, W, T, Border);
	Box(X, Y, T, H, Border);
	Box(X + W - T, Y, T, H, Border);
}

FString ADFHUD::AmmoText(const ADFWeapon* W, bool bLive) {
	// RageWeapon.GetAmmoStatus: "spare/clip"; items without spare clips (grenades, HMG, trip
	// bombs, adrenaline) show the clip count, knife and armour nothing
	if (!W || W->bHideAmmoCount || (W->MaxClipAmmo <= 1 && W->MaxClips <= 1 && !W->bDestroyWhenEmpty))
		return FString();
	if (W->MaxClips <= 1)
		return FString::FromInt(bLive ? W->GetClipAmmo() : W->MaxClipAmmo);
	const int32 Spare = bLive ? W->GetTotalAmmo() : W->MaxClipAmmo * (W->MaxClips - 1);
	return FString::Printf(TEXT("%d/%d"), Spare, bLive ? W->GetClipAmmo() : W->MaxClipAmmo);
}

void ADFHUD::DrawItemCell(const ADFWeapon* W, float X, float Y, float CellW, float CellH, EDFCellState State, const FString& Ammo, const FString& Corner) {
	// MF inventory box: icon on blue, key number top-left, ammo on a bar underneath
	const float BarH = 13.f * UIScale;
	const FLinearColor Fill = State == EDFCellState::Owned ? DFBlue : State == EDFCellState::Available ? DFBlueDark
																									   : DFGreyFill;
	const FLinearColor Border = State == EDFCellState::Unavailable ? Grey : DFBorder;
	Frame(X, Y, CellW, CellH, Fill, Border);
	const FIntRect& R = W->HUDIconRect;
	const float Aspect = float(R.Width()) / FMath::Max(1, R.Height());
	const float IH = FMath::Min(CellH - 4.f * UIScale, (CellW - 6.f * UIScale) / FMath::Max(0.1f, Aspect));
	const float IW = IH * Aspect;
	DrawWeaponIcon(W, X + (CellW - IW) * 0.5f, Y + (CellH - IH) * 0.5f, IH, State == EDFCellState::Unavailable ? Grey : White);
	Text(Corner, X + 3.f * UIScale, Y + 1.f * UIScale, White, 0.55f);
	Frame(X, Y + CellH + 2.f * UIScale, CellW, BarH, State == EDFCellState::Owned ? DFBar : DFGreyFill, Border);
	if (!Ammo.IsEmpty()) {
		float TW = 0.f, TH = 0.f;
		Canvas->TextSize(Font, Ammo, TW, TH, 0.6f * UIScale, 0.6f * UIScale);
		Text(Ammo, X + CellW - TW - 4.f * UIScale, Y + CellH + 1.f * UIScale, White, 0.6f);
	}
}

void ADFHUD::DrawStatus(ADFCharacter* C) {
	// Bottom left like MF: health and armour numbers
	const float X = 16.f * UIScale, Y = Canvas->ClipY - 66.f * UIScale;
	const int32 Health = FMath::Max(0, C->GetHealth());
	Frame(X, Y, 120.f * UIScale, 28.f * UIScale, DFBlueDark, DFBorder);
	Text(TEXT("HEALTH"), X + 6.f * UIScale, Y + 7.f * UIScale, White, 0.55f);
	Text(FString::FromInt(Health), X + 64.f * UIScale, Y + 2.f * UIScale, Health > 25 ? White : FLinearColor(1.f, 0.3f, 0.25f), 1.3f);
	if (const int32 Armour = C->GetArmourCharge(); Armour > 0) {
		Frame(X, Y + 32.f * UIScale, 120.f * UIScale, 28.f * UIScale, DFBlueDark, DFBorder);
		Text(TEXT("ARMOUR"), X + 6.f * UIScale, Y + 39.f * UIScale, White, 0.55f);
		Text(FString::FromInt(Armour), X + 64.f * UIScale, Y + 34.f * UIScale, FLinearColor(0.5f, 0.7f, 1.f), 1.3f);
	}
	if (ADFWeapon* W = C->GetCurrentWeapon())
		Text(W->GetHUDStatus(), Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f + 40.f * UIScale, Yellow, 1.2f, true);
}

void ADFHUD::DrawWeaponBar(ADFCharacter* C) {
	// MF carry strip (bottom centre): one box per carried item, as wide as the carry blocks
	// it uses; free blocks stay as empty frames so you see what still fits
	TArray<ADFWeapon*> Items;
	for (ADFWeapon* W : C->GetInventory())
		if (W)
			Items.Add(W);
	Items.StableSort([](const ADFWeapon& A, const ADFWeapon& B) { return A.InventoryGroup < B.InventoryGroup; });
	const float BlockW = 44.f * UIScale, Gap = 3.f * UIScale, CellH = 30.f * UIScale;
	auto WidthOf = [&](int32 Blocks) { return Blocks * BlockW + (Blocks - 1) * Gap; };
	const int32 FreeBlocks = FMath::Max(0, C->MaxCarry - C->GetCurrentCarry());
	float Total = FreeBlocks > 0 ? WidthOf(FreeBlocks) : 0.f;
	for (const ADFWeapon* W : Items)
		Total += WidthOf(FMath::Max(1, W->GetCarrySize(this))) + Gap;
	float X = Canvas->ClipX * 0.5f - Total * 0.5f;
	const float Y = Canvas->ClipY - CellH - 22.f * UIScale;
	for (ADFWeapon* W : Items) {
		const float CW = WidthOf(FMath::Max(1, W->GetCarrySize(this)));
		DrawItemCell(W, X, Y, CW, CellH, EDFCellState::Owned, AmmoText(W, true), ADFWeapon::SlotKeyName(W->GetSlotKey()));
		if (W == C->GetCurrentWeapon())
			Frame(X - 2.f, Y - 2.f, CW + 4.f, CellH + 4.f, FLinearColor::Transparent, Yellow);
		X += CW + Gap;
	}
	if (FreeBlocks > 0)
		Frame(X, Y, WidthOf(FreeBlocks), CellH, FLinearColor(0.f, 0.f, 0.f, 0.25f), Grey);
	if (GetWorld()->GetTimeSeconds() - SelectedTime < 1.5f)
		Text(SelectedName, Canvas->ClipX * 0.5f, Y - 26.f * UIScale, Yellow, 1.f, true);
}

void ADFHUD::DrawScoreBarMF(float X, float Y, float W, float H, int32 BoxSet, int32 Icon, const FString& Label, float Level) {
	// ScoreBoxes(0) Y=64 blue, (1) Y=0 red, (2) Y=32 grey; ScoreLevelBack X=4 Y=96 W=92 H=32
	static const float BoxY[3] = { 64.f, 0.f, 32.f };
	const int32 B = FMath::Clamp(BoxSet, 0, 2);
	// STY_Alpha on these palette textures comes out about half see-through in MF
	DrawTexRect(TexScoreBoxes, X, Y, W, H, 0.f, BoxY[B], 128.f, 32.f, FLinearColor(1.f, 1.f, 1.f, 0.55f), SE_BLEND_Translucent);
	if (Level > 0.f && Level <= 1.f)
		DrawTexRect(TexScoreBoxes, X + 4.f * RS, Y, 92.f * RS * Level, H, 4.f, 96.f, 92.f * Level, 32.f, SetColour(B == 2 ? 3 : B), SE_BLEND_Additive);
	if (TexScoreIcons) {
		// ScoreIcons(0..2) X=128, Y=0/64/128, 64x64
		DrawTexRect(TexScoreIcons, X + Block * 1.25f, Y - Block * 0.25f, Block, Block, 128.f, 64.f * FMath::Clamp(Icon, 0, 2), 64.f, 64.f,
			FLinearColor::White, SE_BLEND_Translucent);
	}
	const FVector2D TS = DFTextSize(Label, 1);
	DFText(Label, X + (W * 0.75f - TS.X) * 0.5f, Y + (H - TS.Y) * 0.5f, FLinearColor::White, 1);
}

bool ADFHUD::DrawGameInfoMF(ADFGameState* GS) {
	if (!TexScoreBoxes)
		return false;
	// Safecracker (RageSafeCrackerHud): the defenders' safe meter, the countdown above it
	for (TActorIterator<ASafe> It(GetWorld()); It; ++It) {
		if (It->bCrackable) {
			const float SX = Canvas->ClipX - Block * 2.25f;
			float SY = Canvas->ClipY - Block * 0.75f;
			const float Health = FMath::Clamp(It->Health, 0.f, 100.f) / 100.f;
			// MF passes Health / 100 as the fill level (a bar too thin to see): the number tells
			DrawScoreBarMF(SX, SY, Block * 2.f, Block * 0.5f, It->Team, It->Team, FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Health * 100.f)), 0.f);
			SY -= Block * 0.75f;
			const int32 Left = FMath::Max(0, GS->GetRemainingTime());
			DrawScoreBarMF(SX, SY, Block * 2.f, Block * 0.5f, 2, 2, FString::Printf(TEXT("%d:%02d"), Left / 60, Left % 60), 0.f);
			return true;
		}
	}
	if (!GS->IsTeamGame())
		return false;
	// RageTeamHUD.DrawTeamScores: a 64x100 panel per team from the right, score inside
	float SX = Canvas->ClipX - 64.f * RS;
	const float SY = Canvas->ClipY - 100.f * RS;
	for (int32 T = 0; T < FMath::Min(GS->NumTeams, 2); ++T) {
		DrawTexRect(TexBacks, SX, SY, 64.f * RS, 100.f * RS, 64.f, 128.f, 64.f, 100.f, SetColour(T), SE_BLEND_Additive);
		DrawTexRect(TexIcons, SX, SY, 64.f * RS, 100.f * RS, T == 0 ? 64.f : 0.f, 0.f, 64.f, 100.f, FLinearColor::White, SE_BLEND_Translucent);
		const FString S = FString::FromInt(GS->GetTeamScore(T));
		const FVector2D TS = DFTextSize(S, 1);
		DFText(S, SX + (Block - TS.X) * 0.5f, SY + 84.f * RS - TS.Y * 0.5f, FLinearColor::White, 1);
		SX -= Block;
	}
	return true;
}

void ADFHUD::DrawGameInfo(ADFGameState* GS, ADFPlayerController* PC) {
	const float CX = Canvas->ClipX * 0.5f;
	const bool bMFPanels = LoadMFArt() && DrawGameInfoMF(GS);
	if (const int32 Left = GS->GetRemainingTime(); Left >= 0 && !(bMFPanels && GS->GameName == TEXT("SafeCracker")))
		Text(FString::Printf(TEXT("%d:%02d"), Left / 60, Left % 60), CX, 10.f * UIScale, White, 1.2f, true);
	if (GS->IsTeamGame()) {
		const UDFGameSettings& Settings = UDFGameSettings::Get();
		const float Spacing = 140.f * UIScale;
		float X = CX - Spacing * 0.5f * (GS->NumTeams - 1);
		for (int32 T = 0; T < GS->NumTeams && !bMFPanels; ++T, X += Spacing)
			Text(FString::Printf(TEXT("%s %d"), *GS->GetTeamName(T).ToString().ToUpper(), GS->GetTeamScore(T)),
				X, 36.f * UIScale, Settings.GetTeamColor(T), 1.1f, true);
		// Capture points (Holdout / KOH), coloured by the team holding them
		TArray<const AHoldoutPoint*> Points;
		for (TActorIterator<AHoldoutPoint> It(GetWorld()); It; ++It)
			Points.Add(*It);
		const float PSpacing = 110.f * UIScale;
		float PX = CX - PSpacing * 0.5f * (Points.Num() - 1);
		for (const AHoldoutPoint* P : Points) {
			const FLinearColor Col = P->OwnerTeam == DF::NoTeam ? FLinearColor(0.6f, 0.6f, 0.6f) : Settings.GetTeamColor(P->OwnerTeam);
			Text(P->PointName, PX, 60.f * UIScale, Col, 0.9f, true);
			PX += PSpacing;
		}
	} else if (PC && PC->PlayerState) {
		// (MF's DM HUD has no frag counter; bottom right, clear of the minimap)
		const FString Frags = FString::Printf(TEXT("Frags %d"), FMath::RoundToInt(PC->PlayerState->GetScore()));
		const FVector2D FS = DFTextSize(Frags, 1);
		DFText(Frags, Canvas->ClipX - FS.X - Block * 0.25f, Canvas->ClipY - FS.Y - Block * 0.25f, White, 1);
	}
	if (GetWorld()->GetTimeSeconds() - AnnouncementTime < 4.f && !(PC && PC->IsScoreboardShown()) && !GS->HasMatchEnded())
		Text(Announcement, CX, Canvas->ClipY * 0.28f, Yellow, 1.5f, true);
}

void ADFHUD::DrawMessages() {
	// RageHUD.DrawSmallMessages: top left right of the minimap (3.5 blocks), newest last
	const float Now = GetWorld()->GetTimeSeconds();
	Messages.RemoveAll([Now](const FMessage& M) { return Now - M.Time > 6.f; });
	// PromptAndChatFontSize 3 = GetHUDBigFont; line height from "TEST"
	const float X = Block * 3.5f, LineH = DFTextSize(TEXT("TEST"), 2).Y;
	float Y = 2.f;
	for (const FMessage& M : Messages) {
		DFText(M.Text, X, Y, M.Color, 2);
		Y += LineH;
	}
}

void ADFHUD::DrawMiniMapMF(ADFCharacter* C) {
	if (!bLookedForMapInfo) {
		bLookedForMapInfo = true;
		for (TActorIterator<ADFMapInfo> It(GetWorld()); It; ++It)
			MapInfo = *It;
	}
	UTexture2D* Map = MapInfo ? MapInfo->MiniMap.LoadSynchronous() : nullptr;
	if (!Map || !TexMiniIcons)
		return;
	// 1 texture pixel = 64 uu = 144 cm (MiniMapInfo.PixelsToMapUnits)
	const FVector4 B = MapInfo->MiniMapBounds;
	const float CmPerPx = 144.f;
	const float TexW = (B.Z - B.X) / CmPerPx, TexH = (B.W - B.Y) / CmPerPx;
	if (TexW <= 0.f || TexH <= 0.f)
		return;
	const float Size = 192.f * RS, O = Block * 0.125f; // MiniMapSize 192
	const int32 T = TeamSet();
	// background: MF darkens with a team tint (MapBack_Team, STY_Subtractive)
	Box(O, O, Size, Size, T == 1 ? FLinearColor(0.35f, 0.04f, 0.04f, 0.6f) : FLinearColor(0.04f, 0.08f, 0.35f, 0.6f));
	// RageHUD.SetMapExtents (own position only): as zoomed as the map allows, centred on us
	const float Scale = FMath::Max(1.f, FMath::Max(192.f / TexW, 192.f / TexH)) * RS; // screen px per map px
	const float View = Size / Scale;
	const FVector P = C->GetActorLocation();
	const float PX = (P.X - B.X) / CmPerPx, PY = (P.Y - B.Y) / CmPerPx;
	const float MX = FMath::Clamp(PX - View * 0.5f, 0.f, FMath::Max(0.f, TexW - View));
	const float MY = FMath::Clamp(PY - View * 0.5f, 0.f, FMath::Max(0.f, TexH - View));
	DrawTexRect(Map, O, O, Size, Size, MX, MY, View, View, FLinearColor::White, SE_BLEND_Translucent);

	auto ToScreen = [&](const FVector& W) { return FVector2D(O + ((W.X - B.X) / CmPerPx - MX) * Scale, O + ((W.Y - B.Y) / CmPerPx - MY) * Scale); };
	auto Inside = [&](const FVector2D& S) { return S.X >= O && S.Y >= O && S.X <= O + Size && S.Y <= O + Size; };
	const float Icon = 24.f * RS; // MinimapIconSize
	auto DrawIcon = [&](const FVector& W, float U, float V, float Rot = 0.f, bool bRotate = false) {
		const FVector2D S = ToScreen(W);
		if (!Inside(S))
			return;
		const float TW = TexMiniIcons->GetSizeX(), TH = TexMiniIcons->GetSizeY();
		FCanvasTileItem Tile(S - FVector2D(Icon * 0.5f), TexMiniIcons->GetResource(), FVector2D(Icon),
			FVector2D(U / TW, V / TH), FVector2D((U + 32.f) / TW, (V + 32.f) / TH), FLinearColor::White);
		Tile.BlendMode = SE_BLEND_Translucent;
		if (bRotate) {
			Tile.Rotation = FRotator(0.f, Rot, 0.f);
			Tile.PivotPoint = FVector2D(0.5f, 0.5f);
		}
		Canvas->DrawItem(Tile);
	};
	const float TeamU = T == 1 ? 192.f : 224.f; // *_Team(0) X=224, (1) X=192
	// objectives the whole team sees (MinimapInfo.TrackItem): safes, gold, flags, locks
	for (TActorIterator<ASafe> It(GetWorld()); It; ++It)
		DrawIcon(It->GetActorLocation(), It->Team == 1 ? 192.f : 224.f, 192.f); // SafeIcon_Team
	for (TActorIterator<AFlag> It(GetWorld()); It; ++It)
		if (!It->IsHidden() || It->State == EDFFlagState::Held)
			DrawIcon(It->GetActorLocation(), It->Team == 1 ? 192.f : 224.f, 128.f); // GoldIcon_Team
	// Trailer (TrailerGameHUD): delivery bays, the cab coloured by who drives it
	if (const ADFGameState* TGS = GetWorld()->GetGameState<ADFGameState>(); TGS && TGS->GameName == TEXT("Trailer")) {
		for (ADFLegacyActor* L : ADFLegacyActor::FindAll(GetWorld(), TEXT("TrailerDelivery")))
			DrawIcon(L->GetActorLocation(), L->Team == 1 ? 64.f : 96.f, 192.f); // DeliveryIcon_Team
		for (TActorIterator<ATrailerCab> It(GetWorld()); It; ++It) {
			const ADFCharacter* D = It->GetDriver();
			const float U = !D || D->GetTeam() > 1 ? 96.f : (D->GetTeam() == 1 ? 128.f : 160.f); // TrailerIcon_Team
			DrawIcon(It->GetActorLocation(), U, 224.f);
		}
	}
	// team-mates (TeamMateIcon_Team Y=32)
	if (const ADFPlayerState* MyPS = C->GetPlayerState<ADFPlayerState>(); MyPS && MyPS->HasTeam()) {
		for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It)
			if (*It != C && It->IsAlive() && It->GetTeam() == MyPS->GetTeam())
				DrawIcon(It->GetActorLocation(), TeamU, 32.f);
	}
	// us: PlayerIcon_Team (Y=0), turned with the view (MF draws a 3D arrow here). The icon
	// points up the map (-Y) unturned (looked at in MinimapNewIcons), the map has +X right and
	// +Y down, and a canvas turn is clockwise: yaw + 90. The view is the player controller's (in
	// a vehicle the soldier has no controller of their own)
	DrawIcon(P, TeamU, 0.f, (PlayerOwner ? PlayerOwner->GetControlRotation().Yaw : C->GetActorRotation().Yaw) + 90.f, true);

	// border: 16 px pieces around MiniMapSize + 4, team rects in MinimapNewIcons
	const float BX = 6.f * RS, Edge = 16.f * RS, BS = (192.f + 4.f) * RS;
	const float L = T == 1 ? 13.f : 109.f; // TL X; TC +16, TR +54
	auto Piece = [&](float X, float Y, float W, float H, float U, float V) { DrawTexRect(TexMiniIcons, X, Y, W, H, U, V, 16.f, 16.f, FLinearColor::White, SE_BLEND_Translucent); };
	Piece(BX, BX, Edge, Edge, L, 77.f);
	Piece(BX + Edge, BX, BS - 2.f * Edge, Edge, L + 16.f, 77.f);
	Piece(BX + BS - Edge, BX, Edge, Edge, L + 54.f, 77.f);
	Piece(BX, BX + Edge, Edge, BS - 2.f * Edge, L, 93.f);
	Piece(BX + BS - Edge, BX + Edge, Edge, BS - 2.f * Edge, L + 54.f, 93.f);
	Piece(BX, BX + BS - Edge, Edge, Edge, L, 131.f);
	Piece(BX + Edge, BX + BS - Edge, BS - 2.f * Edge, Edge, L + 16.f, 131.f);
	Piece(BX + BS - Edge, BX + BS - Edge, Edge, Edge, L + 54.f, 131.f);
}

void ADFHUD::DrawCaptainMarkers(ADFPlayerController* PC) {
	FVector Eye;
	FRotator ViewRot;
	PC->GetPlayerViewPoint(Eye, ViewRot);
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It) {
		const ADFPlayerState* PS = It->GetPlayerState<ADFPlayerState>();
		if (!PS || !PS->bCaptain || !It->IsAlive() || *It == PC->GetPawn())
			continue;
		const FVector Head = It->GetActorLocation() + FVector(0, 0, 120.f);
		if (FVector::DotProduct(Head - Eye, ViewRot.Vector()) <= 0.f)
			continue;
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("DFCaptainMark"), false, PC->GetPawn());
		Params.AddIgnoredActor(*It);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Head, ECC_Visibility, Params))
			continue; // behind a wall, like the MF sprite
		const FVector S = Project(Head);
		const FString Label = TEXT("CAPTAIN");
		const FVector2D TS = DFTextSize(Label, 0);
		DFText(Label, S.X - TS.X * 0.5f, S.Y - TS.Y, UDFGameSettings::Get().GetTeamColor(PS->GetTeam()), 0);
	}
}

void ADFHUD::DrawCommandMenu(ADFPlayerController* PC) {
	// MF OrdersMainWindow / TargetChildWindow: below the minimap at the left, 32 px rows
	// (on a 1024x768 reference), orders in one column, then who gets it in the next.
	const int32 Stage = PC->GetCommandMenuStage();
	if (Stage == 0)
		return;
	const float Ref = FMath::Min(Canvas->ClipX / 4.f, 256.f) / 256.f;
	const float RowH = 32.f * Ref, ColW = 256.f * Ref, Top = Block * 3.25f;
	const FLinearColor Team = SetColour(TeamSet());
	const FLinearColor Fill(Team.R * 0.25f, Team.G * 0.25f, Team.B * 0.25f, 0.6f);
	// rows of the column being picked from take mouse clicks (GetCommandMenuSlotAt); the one
	// under the cursor lights up
	CommandMenuRows.Reset();
	float MX = -1.f, MY = -1.f;
	PC->GetMousePosition(MX, MY);
	auto Column = [&](float X, float Y0, const TArray<FString>& Items, int32 Highlight, bool bActive) {
		Frame(X, Y0, ColW, RowH * (Items.Num() + 1), Fill, Team);
		for (int32 i = 0; i < Items.Num(); ++i) {
			const float Y = Y0 + RowH * (i + 0.5f);
			const float TY = Y - DFTextSize(TEXT("TEST"), 1).Y * 0.5f + RowH * 0.5f;
			const FBox2D Row(FVector2D(X, Y), FVector2D(X + ColW, Y + RowH));
			const bool bHover = bActive && Row.IsInside(FVector2D(MX, MY));
			if (bActive)
				CommandMenuRows.Add({ Row, i + 1 });
			DFText(FString::Printf(TEXT("%d"), (i + 1) % 10), X + 6.f * Ref, TY, FLinearColor(0.6f, 0.7f, 1.f), 0);
			DFText(Items[i], X + 26.f * Ref, TY, i == Highlight || bHover ? Yellow : White, 1);
		}
	};
	const TArray<FString> Orders = PC->GetCommandMenuOrders();
	Column(0.f, Top, Orders, Stage == 2 ? PC->GetCommandMenuOrder() : -1, Stage == 1);
	if (Stage == 2) {
		TArray<FString> Names = { TEXT("All") };
		for (const APlayerState* PS : PC->GetCommandTargets())
			Names.Add(PS->GetPlayerName());
		Column(ColW, Top + RowH * PC->GetCommandMenuOrder(), Names, -1, true);
	}
}

int32 ADFHUD::GetCommandMenuSlotAt(const FVector2D& Screen) const {
	for (const TPair<FBox2D, int32>& Row : CommandMenuRows)
		if (Row.Key.IsInside(Screen))
			return Row.Value;
	return 0;
}

void ADFHUD::DrawLoadout(ADFCharacter* C, ADFPlayerController* PC) {
	// MF loadout grid (top left): one row per key, every item of that key in the row.
	// Bright = carried, dark = fits, grey = doesn't fit. Rows with several items are
	// opened with their key; then the item's number (shown in yellow) picks it.
	const TArray<TSubclassOf<ADFWeapon>> Items = PC->GetLoadoutItems();
	const int32 OpenSlot = PC->GetLoadoutOpenSlot();
	LoadoutHits.Reset();
	const float CellH = 34.f * UIScale, RowH = CellH + 20.f * UIScale, Gap = 4.f * UIScale;
	const float X0 = 10.f * UIScale;
	float Y = 10.f * UIScale, X = X0;
	int32 RowKey = -1, InRow = 0;
	for (int32 i = 0; i < Items.Num(); ++i) {
		const ADFWeapon* CDO = Items[i]->GetDefaultObject<ADFWeapon>();
		const int32 Key = CDO->GetSlotKey();
		if (Key != RowKey) {
			if (RowKey != -1)
				Y += RowH;
			X = X0;
			RowKey = Key;
			InRow = 0;
		}
		++InRow;
		const ADFWeapon* Have = C->FindWeaponOfClass(Items[i]);
		const EDFCellState State = Have ? EDFCellState::Owned : (C->CanCarry(Items[i]) ? EDFCellState::Available : EDFCellState::Unavailable);
		const FIntRect& R = CDO->HUDIconRect;
		const float CellW = FMath::Max(CellH * 1.25f, FMath::Min(CellH * 4.f, CellH * float(R.Width()) / FMath::Max(1, R.Height())));
		DrawItemCell(CDO, X, Y, CellW, CellH, State, AmmoText(Have ? Have : CDO, Have != nullptr), ADFWeapon::SlotKeyName(Key));
		LoadoutHits.Emplace(FBox2D(FVector2D(X, Y), FVector2D(X + CellW, Y + CellH)), i);
		if (Key == OpenSlot)
			Text(FString::FromInt(InRow % 10), X + CellW - 14.f * UIScale, Y, Yellow, 1.1f);
		if (i == PC->LoadoutCursor)
			Frame(X - 2.f, Y - 2.f, CellW + 4.f, CellH + 4.f, FLinearColor::Transparent, Yellow);
		X += CellW + Gap;
	}
	const FString Hint = PC->UsesTouch() ? TEXT("Tap an item to take it or put it back   -   LOADOUT to leave")
		: OpenSlot                       ? FString::Printf(TEXT("Row %s: press the item's number"), *ADFWeapon::SlotKeyName(OpenSlot))
										 : TEXT("Press Q to exit loadout   -   1-0 / X pick a row, again for an item   -   Up/Down + Fire also work");
	Text(Hint, Canvas->ClipX * 0.5f, Canvas->ClipY * 0.72f, White, 1.f, true);
}

void ADFHUD::DrawScoreboardMF(ADFGameState* GS) {
	const float CW = Canvas->ClipX, CH = Canvas->ClipY;
	const FLinearColor Band(0.f, 0.f, 0.12f, 0.75f);
	// top band: the result (or the game's goal while playing)
	Box(0.f, 0.f, CW, CH * 0.16f, Band);
	const FString Title = GS->HasMatchEnded() ? GS->WinnerText : GS->GameName;
	FVector2D TS = DFTextSize(Title, 2);
	DFText(Title, (CW - TS.X) * 0.5f, CH * 0.088f - TS.Y * 0.5f, FLinearColor::White, 2);
	// bottom band: "<Game> in <Map>", remaining time
	const float BandY = CH * 0.953f;
	Box(0.f, BandY, CW, CH - BandY, Band);
	const FString Where = FString::Printf(TEXT("%s in %s"), *GS->GameName, MapInfo && !MapInfo->Title.IsEmpty() ? *MapInfo->Title : *GetWorld()->GetMapName());
	const int32 Left = FMath::Max(0, GS->GetRemainingTime());
	const FString Time = FString::Printf(TEXT("Remaining Time:  %02d:%02d"), Left / 60, Left % 60);
	TS = DFTextSize(Where, 0);
	DFText(Where, (CW - TS.X) * 0.5f, BandY + 2.f, FLinearColor::White, 0);
	const FVector2D TT = DFTextSize(Time, 0);
	DFText(Time, (CW - TT.X) * 0.5f, BandY + 2.f + TS.Y, FLinearColor::White, 0);
	if (GS->HasMatchEnded()) {
		const FString Over = TEXT("The match is over.  Hit [Fire] to continue!");
		TS = DFTextSize(Over, 0);
		DFText(Over, (CW - TS.X) * 0.5f, CH * 0.905f, FLinearColor::White, 0);
	}

	// squads: ScoreBoxes bars, header opaque, rows see-through, score in its own box
	const TArray<ADFPlayerState*> Players = GS->GetSortedPlayers();
	const float RowH = CH * 0.043f, BarH = RowH * 0.8f;
	auto Bar = [&](float X, float Y, float W, int32 Set, float Alpha) {
		static const float BoxY[3] = { 64.f, 0.f, 32.f };
		DrawTexRect(TexScoreBoxes, X, Y, W, BarH, 0.f, BoxY[FMath::Clamp(Set, 0, 2)], 96.f, 32.f, FLinearColor(1.f, 1.f, 1.f, Alpha), SE_BLEND_Translucent);
	};
	auto Column = [&](float X, float W, int32 Set, const FString& Head, const FString& HeadScore, TFunctionRef<bool(const ADFPlayerState*)> Filter) {
		float Y = CH * 0.255f;
		const float ScoreW = W * 0.12f, NameW = W - ScoreW - 4.f;
		Bar(X, Y, NameW, Set, 0.95f);
		Bar(X + NameW + 4.f, Y, ScoreW, Set, 0.95f);
		const FVector2D H = DFTextSize(Head, 1);
		DFText(Head, X + 10.f, Y + (BarH - H.Y) * 0.5f, FLinearColor::White, 1);
		if (!HeadScore.IsEmpty()) {
			const FVector2D HS = DFTextSize(HeadScore, 1);
			DFText(HeadScore, X + NameW + 4.f + (ScoreW - HS.X) * 0.5f, Y + (BarH - HS.Y) * 0.5f, FLinearColor::White, 1);
		}
		Y += RowH;
		for (const ADFPlayerState* PS : Players) {
			if (!Filter(PS))
				continue;
			Bar(X, Y, NameW, Set, 0.4f);
			Bar(X + NameW + 4.f, Y, ScoreW, Set, 0.4f);
			const FString Score = FString::FromInt(FMath::RoundToInt(PS->GetScore()));
			const FVector2D N = DFTextSize(PS->GetPlayerName(), 1), SS = DFTextSize(Score, 1);
			DFText(PS->GetPlayerName() + (PS->bCaptain ? TEXT(" (C)") : TEXT("")), X + 10.f, Y + (BarH - N.Y) * 0.5f, FLinearColor::White, 1);
			DFText(Score, X + NameW + 4.f + (ScoreW - SS.X) * 0.5f, Y + (BarH - SS.Y) * 0.5f, FLinearColor::White, 1);
			Y += RowH;
		}
	};
	if (GS->IsTeamGame()) {
		const UDFGameSettings& Settings = UDFGameSettings::Get();
		for (int32 T = 0; T < FMath::Min(GS->NumTeams, 2); ++T) {
			FString Name = GS->GetTeamName(T).ToString();
			if (!Name.Contains(TEXT("Squad")))
				Name += TEXT(" Squad");
			Column(T == 0 ? CW * 0.073f : CW * 0.571f, CW * 0.355f, T, Name, FString::FromInt(GS->GetTeamScore(T)),
				[T](const ADFPlayerState* PS) { return PS->GetTeam() == T; });
		}
		// emblem between the columns (the leading squad's)
		const int32 Lead = GS->GetTeamScore(1) > GS->GetTeamScore(0) ? 1 : 0;
		const float EW = Block, EH = Block * 100.f / 64.f;
		DrawTexRect(TexIcons, (CW - EW) * 0.5f, CH * 0.19f, EW, EH, Lead == 0 ? 64.f : 0.f, 0.f, 64.f, 100.f, FLinearColor::White, SE_BLEND_Translucent);
	} else {
		Column(CW * 0.3f, CW * 0.4f, 2, TEXT("Player"), TEXT("Frags"), [](const ADFPlayerState*) { return true; });
	}
}

void ADFHUD::DrawScoreboard(ADFGameState* GS) {
	if (TexScoreBoxes && LoadMFArt()) {
		DrawScoreboardMF(GS);
		return;
	}
	const TArray<ADFPlayerState*> Players = GS->GetSortedPlayers();
	const UDFGameSettings& Settings = UDFGameSettings::Get();
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
	for (const ADFPlayerState* PS : Players) {
		const FLinearColor Col = PS->HasTeam() ? Settings.GetTeamColor(PS->GetTeam()) : White;
		Text(PS->GetPlayerName(), X, Y, Col, 0.9f);
		Text(FString::FromInt(FMath::RoundToInt(PS->GetScore())), X + W * 0.6f, Y, Col, 0.9f);
		Text(FString::FromInt(PS->Deaths), X + W * 0.75f, Y, Col, 0.9f);
		Text(PS->IsABot() ? TEXT("BOT") : FString::FromInt(PS->GetPingInMilliseconds()), X + W * 0.9f, Y, Col, 0.9f);
		Y += RowH;
	}
}

void ADFHUD::DrawDeadScreen(ADFGameState* GS, ADFPlayerController* PC) {
	if (GS->HasMatchEnded() || !PC)
		return;
	const ADFPlayerState* PS = PC->GetPlayerState<ADFPlayerState>();
	const float Left = PS ? PS->DeathTime + GS->RespawnWait - GS->GetServerWorldTimeSeconds() : 0.f;
	Text(Left > 0.f ? FString::Printf(TEXT("Respawning in %d"), FMath::CeilToInt(Left)) : TEXT("Respawning..."),
		Canvas->ClipX * 0.5f, Canvas->ClipY * 0.6f, White, 1.2f, true);
}

// --- MF art --------------------------------------------------------------------------
// Port of RageHUD (Rage.u): TexRects and layout from its defaultproperties / Draw* code.
// Translucent (UE1 STY_Translucent) = additive, tinted by the colour set.

namespace {
	// RageHUD.Colour_Sets: blue, red, white, grey
	const FLinearColor ColourSets[4] = {
		FLinearColor(0.f, 0.f, 1.f), FLinearColor(1.f, 0.f, 0.f), FLinearColor(1.f, 1.f, 1.f),
		FLinearColor(50.f / 255.f, 50.f / 255.f, 50.f / 255.f)
	};
	// Box_Sets_* rows in Rage.Boxes per set (Y): blue 128, red 64, white 0, grey 192
	const float BoxRowY[4] = { 128.f, 64.f, 0.f, 192.f };

	UTexture2D* LoadTex(const TCHAR* Path) {
		return LoadObject<UTexture2D>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
} // namespace

bool ADFHUD::LoadMFArt() {
	if (!bTriedArt) {
		bTriedArt = true;
		TexBoxes = LoadTex(TEXT("/MobileForcesClassic/Textures/Rage/Boxes.Boxes"));
		TexBacks = LoadTex(TEXT("/MobileForcesClassic/Textures/Rage/Backs.Backs"));
		TexIcons = LoadTex(TEXT("/MobileForcesClassic/Textures/Rage/Icons.Icons"));
		TexLoadoutBack[0] = LoadTex(TEXT("/MobileForcesClassic/Textures/Rage/LoadoutBackBlue.LoadoutBackBlue"));
		TexLoadoutBack[1] = LoadTex(TEXT("/MobileForcesClassic/Textures/Rage/LoadoutBackRed.LoadoutBackRed"));
		TexPain[0] = LoadTex(TEXT("/MobileForcesClassic/Textures/Rage/OwBlue.OwBlue"));
		TexPain[1] = LoadTex(TEXT("/MobileForcesClassic/Textures/Rage/OwRed.OwRed"));
		TexScoreBoxes = LoadTex(TEXT("/MobileForcesClassic/Textures/Rage/ScoreBoxes.ScoreBoxes"));
		TexScoreIcons = LoadTex(TEXT("/MobileForcesClassic/Textures/Rage/ScoreIcons.ScoreIcons"));
		TexMiniIcons = LoadTex(TEXT("/MobileForcesClassic/Textures/Rage/MinimapNewIcons.MinimapNewIcons"));
		for (int32 i = 0; i < 24; ++i) {
			const FString Name = FString::Printf(TEXT("CrossHair%c"), TCHAR('A' + i));
			TexCrosshair[i] = LoadTex(*FString::Printf(TEXT("/MobileForcesClassic/Textures/Rage/%s.%s"), *Name, *Name));
		}
	}
	return TexBoxes && TexBacks && TexIcons;
}

void ADFHUD::NotifyPain(const FVector& FromDir) {
	HitTime = GetWorld()->GetTimeSeconds();
	HitDir = FromDir;
}

void ADFHUD::DrawPainMF() {
	// RageHUD.DrawPainIndicators: the screen edge facing the attacker glows in the team's
	// colour for PainFadeTime (1 s); undirected damage lights all four edges
	const float Intensity = 1.f - (GetWorld()->GetTimeSeconds() - HitTime) / 1.f;
	UTexture2D* Tex = TexPain[TeamSet()];
	if (Intensity <= 0.f || !Tex || !PlayerOwner)
		return;
	bool Side[4] = { true, true, true, true }; // left, right, top, bottom
	if (!HitDir.IsNearlyZero()) {
		const float Rel = FMath::FindDeltaAngleDegrees(PlayerOwner->GetControlRotation().Yaw, HitDir.Rotation().Yaw);
		Side[0] = Rel < -45.f && Rel >= -135.f;
		Side[1] = Rel > 45.f && Rel <= 135.f;
		Side[2] = FMath::Abs(Rel) <= 45.f;
		Side[3] = FMath::Abs(Rel) > 135.f;
	}
	const FLinearColor C = FLinearColor::White * Intensity;
	const float SV = Canvas->ClipY / 512.f, SH = Canvas->ClipX / 512.f;
	const float CX = Canvas->ClipX * 0.5f, CY = Canvas->ClipY * 0.5f;
	const float VW = 255.f * SV * 0.5f, VH = 255.f * SV; // left/right pieces
	const float HW = 255.f * SH, HH = 255.f * SH * 0.5f; // top/bottom pieces
	// quarter texture mirrored into place: (U, V, UL, VL) with negative extents flip
	auto Piece = [&](float X, float Y, float W, float H, bool bFlipU, bool bFlipV) {
		DrawTexRect(Tex, X, Y, W, H, bFlipU ? 255.f : 1.f, bFlipV ? 255.f : 1.f, bFlipU ? -254.f : 254.f, bFlipV ? -254.f : 254.f,
			C, SE_BLEND_Additive);
	};
	if (Side[0]) {
		Piece(0.f, CY - VH, VW, VH, false, false);
		Piece(0.f, CY, VW, VH, false, true);
	}
	if (Side[1]) {
		Piece(Canvas->ClipX - VW, CY - VH, VW, VH, true, false);
		Piece(Canvas->ClipX - VW, CY, VW, VH, true, true);
	}
	if (Side[2]) {
		Piece(CX - HW, 0.f, HW, HH, true, true);
		Piece(CX, 0.f, HW, HH, false, true);
	}
	if (Side[3]) {
		Piece(CX - HW, Canvas->ClipY - HH, HW, HH, true, false);
		Piece(CX, Canvas->ClipY - HH, HW, HH, false, false);
	}
}

int32 ADFHUD::LoadoutItemAt(const FVector2D& Point) const {
	for (const TPair<FBox2D, int32>& Hit : LoadoutHits)
		if (Hit.Key.IsInside(Point))
			return Hit.Value;
	return INDEX_NONE;
}

FString ADFHUD::LoadoutKeyName() {
	// the player's Loadout key (Options > Controls), upper case like MF's hint
	for (const FDFBinding& B : ADFPlayerController::GetBindings())
		if (B.Action == TEXT("Loadout"))
			return ADFPlayerController::GetBoundKey(B).GetDisplayName().ToString().ToUpper();
	return TEXT("Q");
}

void ADFHUD::ChangeCrosshair(int32 Delta) {
	UDFUserSettings& S = UDFUserSettings::Get();
	S.Crosshair = DF::NextCrosshair(S.Crosshair, Delta);
	S.SaveSettings();
}

void ADFHUD::ChangeHUDSize(int32 Delta) {
	UDFUserSettings& S = UDFUserSettings::Get();
	S.HUDSize = FMath::Clamp(S.HUDSize + Delta, 0, 4);
	S.SaveSettings();
}

void ADFHUD::DrawTexRect(UTexture2D* Tex, float X, float Y, float W, float H, float U, float V, float UL, float VL,
	const FLinearColor& Color, ESimpleElementBlendMode Blend) {
	if (!Tex || W <= 0.f || H <= 0.f)
		return;
	const float TW = Tex->GetSizeX(), TH = Tex->GetSizeY();
	FCanvasTileItem Tile(FVector2D(X, Y), Tex->GetResource(), FVector2D(W, H),
		FVector2D(U / TW, V / TH), FVector2D((U + UL) / TW, (V + VL) / TH), Color);
	Tile.BlendMode = Blend;
	Canvas->DrawItem(Tile);
}

int32 ADFHUD::TeamSet() const {
	// RageHUD.TeamIndex: red team -> 1, everyone else blue
	const APlayerController* PC = PlayerOwner;
	const ADFPlayerState* PS = PC ? PC->GetPlayerState<ADFPlayerState>() : nullptr;
	return PS && PS->GetTeam() == 1 ? 1 : 0;
}

FLinearColor ADFHUD::SetColour(int32 Set) const {
	// Teams beyond MF's two keep their configured colour for fills and bars
	if (Set <= 1) {
		const APlayerController* PC = PlayerOwner;
		const ADFPlayerState* PS = PC ? PC->GetPlayerState<ADFPlayerState>() : nullptr;
		if (PS && PS->HasTeam() && PS->GetTeam() > 1)
			return UDFGameSettings::Get().GetTeamColor(PS->GetTeam());
	}
	return ColourSets[FMath::Clamp(Set, 0, 3)];
}

void ADFHUD::DrawWeapIconMF(const ADFWeapon* W, float X, float Y, int32 Set, const FString& Ammo, const FString& Corner) {
	// RageHUD.DrawWeapIcon: box (single or left/middle.../right), translucent back in the
	// set colour, ammo bar along the bottom quarter, ammo text, group key, icon centred
	const int32 Count = FMath::Max(1, W->GetCarrySize(this));
	const int32 MaxAmmo = FMath::Max(1, W->MaxClips * W->MaxClipAmmo);
	float AmmoW = 64.f * Count * FMath::Clamp(float(W->GetAmmoCount()) / MaxAmmo, 0.f, 1.f);
	if (!W->GetOwner())
		AmmoW = 64.f * Count; // store item: full
	const FLinearColor Back = SetColour(Set);
	const FLinearColor Bar = SetColour(Set == 2 ? TeamSet() : Set); // RageHUD.SetAmmoBarColour
	for (int32 i = 0; i < Count; ++i) {
		// column in the atlases: 0 left, 64 middle, 128 right, 192 single
		const float Col = Count == 1 ? 192.f : (i == 0 ? 0.f : (i == Count - 1 ? 128.f : 64.f));
		const float BX = X + i * Block;
		// ammo filling from the right: this block's share
		const float Seg = FMath::Clamp(AmmoW - (Count - 1 - i) * 64.f, 0.f, 64.f);
		if (Seg > 0.f)
			DrawTexRect(TexBacks, BX + Block - Seg * RS, Y + Block * 0.75f, Seg * RS, Block * 0.25f,
				Col + 64.f - Seg, 48.f, Seg, 16.f, Bar, SE_BLEND_Additive);
		DrawTexRect(TexBacks, BX, Y, Block, Block * 0.75f, Col, 0.f, 64.f, 48.f, Back, SE_BLEND_Additive);
		DrawTexRect(TexBoxes, BX, Y, Block, Block, Col, BoxRowY[FMath::Clamp(Set, 0, 3)], 64.f, 64.f,
			FLinearColor::White, SE_BLEND_Translucent);
	}
	// weapon picture: native width x RenderScale, block high, centred (grey when no room)
	TObjectPtr<UTexture2D>& Icon = IconCache.FindOrAdd(W->GetClass());
	if (!Icon && !W->HUDIcon.IsEmpty())
		Icon = DF::LoadClassic<UTexture2D>(W->HUDIcon);
	if (Icon) {
		const FIntRect& R = W->HUDIconRect;
		const float IW = R.Width() * RS * Block / FMath::Max(1.f, R.Height() * RS);
		DrawTexRect(Icon, X + Count * Block * 0.5f - IW * 0.5f, Y, IW, Block, R.Min.X, R.Min.Y, R.Width(), R.Height(),
			Set == 3 ? ColourSets[3] * 3.f : FLinearColor::White, SE_BLEND_Translucent);
	}
	// ammo bottom right (small font), key top left in a pale team colour
	if (!Ammo.IsEmpty()) {
		float TW = 0.f, TH = 0.f;
		Canvas->TextSize(GEngine->GetSmallFont(), Ammo, TW, TH, RS, RS);
		FCanvasTextItem T(FVector2D(X + Count * Block - TW - 8.f * RS, Y + Block - TH - 2.f * RS), FText::FromString(Ammo),
			GEngine->GetSmallFont(), FLinearColor::White);
		T.Scale = FVector2D(RS);
		Canvas->DrawItem(T);
	}
	if (Set != 3 && !Corner.IsEmpty()) {
		const FLinearColor Key = TeamSet() == 1 ? FLinearColor(1.f, 0.75f, 0.75f) : FLinearColor(0.75f, 0.75f, 1.f);
		float TW = 0.f, TH = 0.f;
		Canvas->TextSize(GEngine->GetSmallFont(), Corner, TW, TH, RS, RS);
		FCanvasTextItem T(FVector2D(X + (20.f * RS - TW) * 0.5f, Y + (26.f * RS - TH) * 0.5f), FText::FromString(Corner),
			GEngine->GetSmallFont(), Key);
		T.Scale = FVector2D(RS);
		Canvas->DrawItem(T);
	}
}

void ADFHUD::DrawEmptyMF(int32 Blocks, float X, float Y) {
	// RageHUD.DrawEmptyIcon: grey translucent back + grey frame per free carry block
	for (int32 i = 0; i < Blocks; ++i) {
		const float Col = Blocks == 1 ? 192.f : (i == 0 ? 0.f : (i == Blocks - 1 ? 128.f : 64.f));
		DrawTexRect(TexBacks, X + i * Block, Y, Block, Block * 0.75f, Col, 0.f, 64.f, 48.f, ColourSets[3], SE_BLEND_Additive);
		DrawTexRect(TexBoxes, X + i * Block, Y, Block, Block, Col, 192.f, 64.f, 64.f, FLinearColor::White, SE_BLEND_Translucent);
	}
}

void ADFHUD::DrawInventoryMF(ADFCharacter* C, ADFPlayerController* PC) {
	// RageHUD.DrawInventory at (ClipX/2 - 3.5 blocks, bottom): carried items, knife one block
	TArray<ADFWeapon*> Items;
	for (ADFWeapon* W : C->GetInventory())
		if (W)
			Items.Add(W);
	Items.StableSort([](const ADFWeapon& A, const ADFWeapon& B) { return A.InventoryGroup < B.InventoryGroup; });
	const bool bLoadout = C->IsInLoadout();
	// selected: the loadout cursor in the loadout; in play the weapon in hand (or coming up) -
	// MF showed that with the first-person model, which we don't have yet
	const ADFWeapon* Selected = bLoadout ? PC->GetLoadoutCursorWeapon()
										 : (C->GetPendingWeapon() ? C->GetPendingWeapon() : C->GetCurrentWeapon());
	float X = Canvas->ClipX * 0.5f - Block * 3.5f;
	const float Y = Canvas->ClipY - Block;
	int32 Left = C->MaxCarry;
	for (ADFWeapon* W : Items) {
		DrawWeapIconMF(W, X, Y, W == Selected ? 2 : TeamSet(), AmmoText(W, true), ADFWeapon::SlotKeyName(W->GetSlotKey()));
		X += FMath::Max(1, W->GetCarrySize(this)) * Block;
		Left -= W->GetCarrySize(this);
	}
	if (Left > 0)
		DrawEmptyMF(Left, X, Y);
	if (!bLoadout && GetWorld()->GetTimeSeconds() - SelectedTime < 1.5f)
		Text(SelectedName, Canvas->ClipX * 0.5f, Y - 28.f * RS, Yellow, RS, true);
}

void ADFHUD::DrawHealthMF(ADFCharacter* C) {
	const int32 T = TeamSet();
	const FLinearColor Fill = SetColour(T);
	// Health: soldier silhouette filled from the bottom (RageHUD.DrawHealth)
	const float HX = 0.f, HY = Canvas->ClipY - 128.f * RS;
	// filled up to MaxHealth (ZombieHUD.DrawHealth: zombies' 333 is a full soldier, not 3 of them)
	const int32 Health = FMath::Max(C->GetHealth(), 0);
	const float Lvl = 128.f * FMath::Min(Health, C->MaxHealth) / FMath::Max(C->MaxHealth, 1);
	DrawTexRect(TexBacks, HX, HY + (128.f - Lvl) * RS, 64.f * RS, Lvl * RS, 0.f, 64.f + 128.f - Lvl, 64.f, Lvl, Fill, SE_BLEND_Additive);
	DrawTexRect(TexIcons, HX, HY, 64.f * RS, 128.f * RS, T == 1 ? 128.f : 192.f, 0.f, 64.f, 128.f, FLinearColor::White, SE_BLEND_Translucent);
	// RageHUD.DrawHealth: GetHUDMedFont, at ((64 + 8) * RS / 2 - W * 0.75, 48 * RS - H / 2)
	const FString HS = FString::FromInt(Health);
	FVector2D TS = DFTextSize(HS, 1);
	DFText(HS, HX + 72.f * RS * 0.5f - TS.X * 0.75f, HY + 48.f * RS - TS.Y * 0.5f, FLinearColor::White, 1);
	// Armour: vest next to it, only while carrying armour (RageHUD.DrawArmour)
	if (const int32 Armour = C->GetArmourCharge(); Armour > 0) {
		const float AX = 46.f * RS, AY = Canvas->ClipY - 56.f * RS;
		const float AL = 56.f * FMath::Min(Armour, 100) / 100.f;
		DrawTexRect(TexBacks, AX, AY + (56.f - AL) * RS, 68.f * RS, AL * RS, 0.f, 192.f + 56.f - AL, 64.f, AL, Fill, SE_BLEND_Additive);
		DrawTexRect(TexIcons, AX, AY, 64.f * RS, 56.f * RS, T == 1 ? 128.f : 192.f, 128.f, 64.f, 56.f, FLinearColor::White, SE_BLEND_Translucent);
		const FString AS = FString::FromInt(Armour);
		TS = DFTextSize(AS, 1);
		DFText(AS, AX + 32.f * RS - TS.X * 0.75f, AY + 28.f * RS - TS.Y * 0.5f, FLinearColor::White, 1);
	}
	// Loadout available here (RageHUD: CanDoLoadout -> Loadout_Back + Loadout_Team)
	if (!C->IsInLoadout() && UDFLoadoutLibrary::FindLoadoutSourceFor(C)) {
		const float LX = 64.f * RS, LY = Canvas->ClipY - Block * 2.f;
		DrawTexRect(TexBacks, LX, LY, 64.f * RS, 32.f * RS, 64.f, 96.f, 64.f, 32.f, Fill, SE_BLEND_Additive);
		DrawTexRect(TexIcons, LX, LY, 64.f * RS, 32.f * RS, T == 1 ? 0.f : 64.f, 160.f, 64.f, 32.f, FLinearColor::White, SE_BLEND_Translucent);
	}
}

void ADFHUD::DrawLoadoutMF(ADFCharacter* C, ADFPlayerController* PC) {
	// Backdrop: the team's emblem in the middle (RageHUD.DrawLoadoutBackdrop, 510 at 800x600)
	if (UTexture2D* Back = TexLoadoutBack[TeamSet()]) {
		const float MW = 510.f * Canvas->ClipX / 800.f, MH = 510.f * Canvas->ClipY / 600.f;
		DrawTexRect(Back, (Canvas->ClipX - MW) * 0.5f, (Canvas->ClipY - MH) * 0.5f, MW, MH, 1.f, 1.f, 255.f, 255.f,
			FLinearColor::White, SE_BLEND_Additive);
	}
	// Store rows from the top left: one row per key, items side by side (RageHUD.DrawLoadout)
	const TArray<TSubclassOf<ADFWeapon>> Items = PC->GetLoadoutItems();
	const int32 OpenSlot = PC->GetLoadoutOpenSlot();
	LoadoutHits.Reset();
	float Y = 16.f - Block, X = 0.f;
	int32 RowKey = -1, InRow = 0;
	for (int32 i = 0; i < Items.Num(); ++i) {
		const ADFWeapon* CDO = Items[i]->GetDefaultObject<ADFWeapon>();
		const int32 Key = CDO->GetSlotKey();
		if (Key != RowKey) {
			Y += Block;
			X = 0.f;
			RowKey = Key;
			InRow = 0;
		}
		++InRow;
		const float W = FMath::Max(1, CDO->GetCarrySize(this)) * Block;
		if (X > 0.f && X + W > Canvas->ClipX) {
			X = 0.f; // wrap like MF when a row doesn't fit
			Y += Block;
		}
		const int32 Set = i == PC->LoadoutCursor ? 2 : (!C->CanCarry(Items[i]) ? 3 : TeamSet());
		const ADFWeapon* Owned = C->FindWeaponOfClass(Items[i]);
		const ADFWeapon* Shown = Owned ? Owned : CDO;
		DrawWeapIconMF(Shown, X, Y, Set, AmmoText(Shown, Owned != nullptr), ADFWeapon::SlotKeyName(Key));
		LoadoutHits.Emplace(FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + Block)), i);
		if (Key == OpenSlot)
			Text(FString::FromInt(InRow % 10), X + W - 16.f * RS, Y + 2.f, Yellow, RS);
		X += W;
	}
	const FString Msg = PC->UsesTouch() ? FString(TEXT("Tap an item to take it or put it back - LOADOUT to leave"))
		: OpenSlot                      ? FString::Printf(TEXT("Row %s: press the item's number"), *ADFWeapon::SlotKeyName(OpenSlot))
										: FString::Printf(TEXT("Press BACKSPACE or %s to Exit Loadout"), *LoadoutKeyName()); // RagePlayer loadout hint
	const FVector2D MS = DFTextSize(Msg, 1);
	DFText(Msg, (Canvas->ClipX - MS.X) * 0.5f, Canvas->ClipY - Block * 4.f, White, 1);
}
