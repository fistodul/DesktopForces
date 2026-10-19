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

void AMFHUD::DrawStatus(AMFCharacter* C)
{
	const float Y = Canvas->ClipY - 60.f * UIScale;
	const float X = 20.f * UIScale;
	const int32 Health = FMath::Max(0, C->GetHealth());
	Box(X - 6, Y - 6, 230.f * UIScale, 52.f * UIScale, Dim);
	Text(FString::Printf(TEXT("HEALTH %d"), Health), X, Y, Health > 25 ? White : FLinearColor(1.f, 0.3f, 0.25f), 1.2f);
	if (const int32 Armour = C->GetArmourCharge(); Armour > 0)
	{
		Text(FString::Printf(TEXT("ARMOUR %d"), Armour), X, Y + 22.f * UIScale, FLinearColor(0.3f, 0.5f, 1.f), 1.f);
	}
	if (AMFWeapon* W = C->GetCurrentWeapon())
	{
		const float RX = Canvas->ClipX - 260.f * UIScale;
		Box(RX - 6, Y - 6, 250.f * UIScale, 52.f * UIScale, Dim);
		Text(W->DisplayName.ToString(), RX, Y, Yellow, 1.f);
		const FString Ammo = W->MaxClipAmmo > 1 || W->MaxClips > 1
			? FString::Printf(TEXT("%d / %d"), W->GetClipAmmo(), W->GetTotalAmmo())
			: FString::Printf(TEXT("%d"), W->GetClipAmmo());
		Text(Ammo, RX, Y + 22.f * UIScale, White, 1.2f);
		Text(W->GetHUDStatus(), Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f + 40.f * UIScale, Yellow, 1.2f, true);
	}
}

void AMFHUD::DrawWeaponBar(AMFCharacter* C)
{
	// MF inventory strip: carried items by group, original icons, ammo badge under each
	TArray<AMFWeapon*> Items;
	for (AMFWeapon* W : C->GetInventory())
	{
		if (W)
		{
			Items.Add(W);
		}
	}
	Items.Sort([](const AMFWeapon& A, const AMFWeapon& B) { return A.InventoryGroup < B.InventoryGroup; });
	const float H = 40.f * UIScale, Pad = 6.f * UIScale;
	float Total = 0.f;
	for (const AMFWeapon* W : Items)
	{
		Total += H * float(W->HUDIconRect.Width()) / FMath::Max(1, W->HUDIconRect.Height()) + Pad;
	}
	float X = Canvas->ClipX * 0.5f - Total * 0.5f;
	const float Y = Canvas->ClipY - H - 26.f * UIScale;
	for (AMFWeapon* W : Items)
	{
		const bool bCur = W == C->GetCurrentWeapon();
		const float IconW = H * float(W->HUDIconRect.Width()) / FMath::Max(1, W->HUDIconRect.Height());
		Box(X - 2, Y - 2, IconW + 4, H + 18.f * UIScale, bCur ? FLinearColor(0.9f, 0.7f, 0.1f, 0.45f) : Dim);
		DrawWeaponIcon(W, X, Y, H, W->bSelectable ? White : Grey);
		Text(FString::FromInt(W->InventoryGroup % 10), X + 2, Y, Yellow, 0.6f);
		if (W->MaxClipAmmo > 1 || W->MaxClips > 1 || W->bDestroyWhenEmpty)
		{
			Text(FString::FromInt(W->GetAmmoCount()), X + IconW * 0.5f, Y + H, White, 0.6f, true);
		}
		X += IconW + Pad;
	}
	Text(FString::Printf(TEXT("CARRY %d/%d"), C->GetCurrentCarry(), C->MaxCarry), Canvas->ClipX * 0.5f, Y - 18.f * UIScale, White, 0.7f, true);
	if (GetWorld()->GetTimeSeconds() - SelectedTime < 1.5f)
	{
		Text(SelectedName, Canvas->ClipX * 0.5f, Y - 40.f * UIScale, Yellow, 1.f, true);
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
	const TArray<TSubclassOf<AMFWeapon>> Items = PC->GetLoadoutItems();
	const float W = 460.f * UIScale, RowH = 34.f * UIScale;
	const float X = Canvas->ClipX * 0.5f - W * 0.5f;
	float Y = Canvas->ClipY * 0.5f - (Items.Num() * RowH) * 0.5f - 40.f * UIScale;
	Box(X - 10, Y - 10, W + 20, Items.Num() * RowH + 80.f * UIScale, FLinearColor(0.f, 0.f, 0.f, 0.75f));
	Text(TEXT("LOADOUT"), Canvas->ClipX * 0.5f, Y, Yellow, 1.4f, true);
	Y += 36.f * UIScale;
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const AMFWeapon* CDO = Items[i] ? Items[i]->GetDefaultObject<AMFWeapon>() : nullptr;
		if (!CDO)
		{
			continue;
		}
		const int32 Have = C->CountOfClass(Items[i]);
		const bool bFits = C->CanCarry(Items[i]);
		if (i == PC->LoadoutCursor)
		{
			Box(X, Y - 2, W, RowH - 2, FLinearColor(0.9f, 0.7f, 0.1f, 0.5f));
		}
		const FLinearColor Col = Have > 0 ? Yellow : (bFits ? White : Grey);
		const float IconW = DrawWeaponIcon(CDO, X + 6, Y, RowH - 6, bFits || Have > 0 ? White : Grey);
		const FString Label = CDO->DisplayName.ToString() + (Have > 1 ? FString::Printf(TEXT(" x%d"), Have) : (Have == 1 ? TEXT(" *") : TEXT("")));
		Text(Label, X + FMath::Max(IconW, 100.f * UIScale) + 16.f * UIScale, Y + 4.f * UIScale, Col, 1.f);
		Text(FString::FromInt(CDO->CarrySize), X + W - 30.f * UIScale, Y + 4.f * UIScale, Col, 1.f);
		Y += RowH;
	}
	Text(FString::Printf(TEXT("Carry %d / %d   -   Up/Down select, Fire/E take or return, Q leave"), C->GetCurrentCarry(), C->MaxCarry),
		Canvas->ClipX * 0.5f, Y + 6.f * UIScale, White, 0.75f, true);
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
