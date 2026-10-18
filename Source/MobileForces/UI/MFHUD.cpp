#include "UI/MFHUD.h"
#include "Player/MFCharacter.h"
#include "Player/MFPlayerController.h"
#include "Game/MFGameState.h"
#include "Game/MFPlayerState.h"
#include "Weapons/MFWeapon.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"

namespace
{
	const FLinearColor White(1.f, 1.f, 1.f);
	const FLinearColor Yellow(1.f, 0.85f, 0.2f);
	const FLinearColor Dim(0.f, 0.f, 0.f, 0.55f);
	const FLinearColor Blue(0.3f, 0.5f, 1.f);
	const FLinearColor Red(1.f, 0.3f, 0.25f);
}

AMFHUD::AMFHUD()
{
	PrimaryActorTick.bCanEverTick = false;
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
	if (GS)
	{
		DrawGameInfo(GS, PC);
		DrawKillFeed(GS);
		if ((PC && PC->IsScoreboardShown()) || GS->bMatchOver)
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
		// Scope mask (MF RageWeapon.DrawScopeMask): black outside a circle-ish window
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
	Text(FString::Printf(TEXT("HEALTH %d"), Health), X, Y, Health > 25 ? White : Red, 1.2f);
	const int32 Armour = C->GetArmourCharge();
	if (Armour > 0)
	{
		Text(FString::Printf(TEXT("ARMOUR %d"), Armour), X, Y + 22.f * UIScale, Blue, 1.f);
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
		const FString Status = W->GetHUDStatus();
		if (!Status.IsEmpty())
		{
			Text(Status, Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f + 40.f * UIScale, Yellow, 1.2f, true);
		}
	}
}

void AMFHUD::DrawWeaponBar(AMFCharacter* C)
{
	// Carried items by inventory group (number keys), like MF's inventory strip
	TArray<AMFWeapon*> Items;
	for (AMFWeapon* W : C->GetInventory())
	{
		if (W)
		{
			Items.Add(W);
		}
	}
	Items.Sort([](const AMFWeapon& A, const AMFWeapon& B) { return A.InventoryGroup < B.InventoryGroup; });
	const float SlotW = 92.f * UIScale, SlotH = 30.f * UIScale;
	float X = Canvas->ClipX * 0.5f - Items.Num() * SlotW * 0.5f;
	const float Y = Canvas->ClipY - 46.f * UIScale;
	for (AMFWeapon* W : Items)
	{
		const bool bCur = W == C->GetCurrentWeapon();
		Box(X, Y, SlotW - 4, SlotH, bCur ? FLinearColor(0.9f, 0.7f, 0.1f, 0.6f) : Dim);
		const FString Label = FString::Printf(TEXT("%d %s"), W->InventoryGroup % 10, *W->DisplayName.ToString().Left(8));
		Text(Label, X + 4, Y + 2, bCur ? FLinearColor::Black : White, 0.7f);
		X += SlotW;
	}
	Text(FString::Printf(TEXT("CARRY %d/%d"), C->GetCurrentCarry(), C->MaxCarry), Canvas->ClipX * 0.5f, Y - 18.f * UIScale, White, 0.7f, true);
}

void AMFHUD::DrawGameInfo(AMFGameState* GS, AMFPlayerController* PC)
{
	const float CX = Canvas->ClipX * 0.5f;
	if (GS->TimeLimit > 0)
	{
		Text(FString::Printf(TEXT("%d:%02d"), GS->RemainingTime / 60, GS->RemainingTime % 60), CX, 10.f * UIScale, White, 1.2f, true);
	}
	if (GS->bTeamGame)
	{
		Text(FString::Printf(TEXT("BLUE %d"), GS->TeamScores[0]), CX - 140.f * UIScale, 10.f * UIScale, Blue, 1.2f, true);
		Text(FString::Printf(TEXT("RED %d"), GS->TeamScores[1]), CX + 140.f * UIScale, 10.f * UIScale, Red, 1.2f, true);
	}
	else if (PC && PC->PlayerState)
	{
		Text(FString::Printf(TEXT("FRAGS %d"), FMath::RoundToInt(PC->PlayerState->GetScore())), 20.f * UIScale, 10.f * UIScale, White, 1.f);
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - GS->AnnouncementTime < 4.f)
	{
		Text(GS->Announcement, CX, Canvas->ClipY * 0.28f, Yellow, 1.5f, true);
	}
}

void AMFHUD::DrawKillFeed(AMFGameState* GS)
{
	const float Now = GetWorld()->GetTimeSeconds();
	float Y = 40.f * UIScale;
	for (const FMFKillMessage& M : GS->KillFeed)
	{
		if (Now - M.Time > 6.f)
		{
			continue;
		}
		const FString Line = M.Killer.IsEmpty() ? FString::Printf(TEXT("%s %s"), *M.Victim, *M.Verb)
			: FString::Printf(TEXT("%s %s %s"), *M.Killer, *M.Verb, *M.Victim);
		Text(Line, Canvas->ClipX - 20.f * UIScale - 300.f * UIScale, Y, White, 0.8f);
		Y += 18.f * UIScale;
	}
}

void AMFHUD::DrawLoadout(AMFCharacter* C, AMFPlayerController* PC)
{
	const TArray<TSubclassOf<AMFWeapon>> Items = PC->GetLoadoutItems();
	const float W = 420.f * UIScale, RowH = 28.f * UIScale;
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
		const FLinearColor Col = Have > 0 ? Yellow : (bFits ? White : FLinearColor(0.5f, 0.5f, 0.5f));
		Text(FString::Printf(TEXT("%s%s"), *CDO->DisplayName.ToString(), Have > 1 ? *FString::Printf(TEXT(" x%d"), Have) : (Have == 1 ? TEXT(" *") : TEXT(""))), X + 8, Y, Col, 1.f);
		Text(FString::Printf(TEXT("%d"), CDO->CarrySize), X + W - 30.f * UIScale, Y, Col, 1.f);
		Y += RowH;
	}
	Text(FString::Printf(TEXT("Carry %d / %d   -   Up/Down select, Fire/E take or return, Q leave"), C->GetCurrentCarry(), C->MaxCarry),
		Canvas->ClipX * 0.5f, Y + 6.f * UIScale, White, 0.75f, true);
}

void AMFHUD::DrawScoreboard(AMFGameState* GS)
{
	const TArray<AMFPlayerState*> Players = GS->GetSortedPlayers();
	const float W = 520.f * UIScale, RowH = 22.f * UIScale;
	const float X = Canvas->ClipX * 0.5f - W * 0.5f;
	float Y = 90.f * UIScale;
	Box(X - 10, Y - 10, W + 20, (Players.Num() + 2) * RowH + 20.f * UIScale, FLinearColor(0.f, 0.f, 0.f, 0.7f));
	Text(GS->bMatchOver ? GS->WinnerText : GS->GameName, Canvas->ClipX * 0.5f, Y, Yellow, 1.2f, true);
	Y += RowH * 1.5f;
	Text(TEXT("PLAYER"), X, Y, Yellow, 0.8f);
	Text(TEXT("FRAGS"), X + W * 0.6f, Y, Yellow, 0.8f);
	Text(TEXT("DEATHS"), X + W * 0.75f, Y, Yellow, 0.8f);
	Text(TEXT("PING"), X + W * 0.9f, Y, Yellow, 0.8f);
	Y += RowH;
	for (const AMFPlayerState* PS : Players)
	{
		const FLinearColor Col = !GS->bTeamGame ? White : (PS->GetTeam() == EMFTeam::Blue ? Blue : Red);
		Text(PS->GetPlayerName(), X, Y, Col, 0.9f);
		Text(FString::FromInt(FMath::RoundToInt(PS->GetScore())), X + W * 0.6f, Y, Col, 0.9f);
		Text(FString::FromInt(PS->Deaths), X + W * 0.75f, Y, Col, 0.9f);
		Text(FString::FromInt(PS->GetPingInMilliseconds()), X + W * 0.9f, Y, Col, 0.9f);
		Y += RowH;
	}
}

void AMFHUD::DrawDeadScreen(AMFGameState* GS, AMFPlayerController* PC)
{
	if (GS->bMatchOver || !PC)
	{
		return;
	}
	const AMFPlayerState* PS = PC->GetPlayerState<AMFPlayerState>();
	const float Left = PS ? PS->DeathTime + GS->RespawnWait - GS->GetServerWorldTimeSeconds() : 0.f;
	Text(Left > 0.f ? FString::Printf(TEXT("Respawning in %d"), FMath::CeilToInt(Left)) : TEXT("Respawning..."),
		Canvas->ClipX * 0.5f, Canvas->ClipY * 0.6f, White, 1.2f, true);
}
