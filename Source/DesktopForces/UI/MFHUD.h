#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MFHUD.generated.h"

class AMFCharacter;
class AMFGameState;
class AMFPlayerController;
class AMFWeapon;
class UTexture2D;

enum class EMFCellState : uint8 { Owned, Available, Unavailable };

/**
 * Canvas HUD laid out like MF's RageHUD: loadout grid top left (a row per key), carry
 * strip bottom centre (boxes as wide as their carry blocks), health/armour bottom left,
 * clock + team scores + capture points top centre, kill feed, announcements, scoreboard.
 * Canvas drawing is a handful of draw calls with no widget tree to tick - good for low-end
 * and mobile. Messages arrive through UMFDeathMessage / UMFAnnouncement.
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API AMFHUD : public AHUD
{
	GENERATED_BODY()

public:
	/** Orders menu slot (1..) under a screen point, 0 for none. */
	int32 GetCommandMenuSlotAt(const FVector2D& Screen) const;
	AMFHUD();
	virtual void DrawHUD() override;

	void AddKillMessage(const FString& Killer, const FString& Victim, const FString& Verb);
	/** A line in MF's message list (top left: kills, chat, events). */
	void AddMessage(const FString& Line, const FLinearColor& Color);
	void Announce(const FString& Text);
	/** Show the selected weapon's name for a moment (weapon switching). */
	void NotifyWeaponSelected(const AMFWeapon* Weapon);
	/** Damage flash towards the attacker (zero vector: all sides). */
	void NotifyPain(const FVector& FromDir);
	/** MF GrowHUD / ShrinkHUD. */
	void ChangeHUDSize(int32 Delta);

	/** MF ChangeCrosshair (UMFUserSettings::Crosshair, HUDSize: Options > Game). */
	void ChangeCrosshair(int32 Delta);

protected:
	void DrawCrosshair(AMFCharacter* C);
	void DrawStatus(AMFCharacter* C);
	void DrawWeaponBar(AMFCharacter* C);
	void DrawGameInfo(AMFGameState* GS, AMFPlayerController* PC);
	void DrawMessages();
	void DrawCommandMenu(AMFPlayerController* PC);
	/** Rows of the orders menu drawn last frame: screen box -> key slot. */
	TArray<TPair<FBox2D, int32>> CommandMenuRows;
	/** Captains: label over each captain in sight (MF captain ID sprite). */
	void DrawCaptainMarkers(AMFPlayerController* PC);
	void DrawLoadout(AMFCharacter* C, AMFPlayerController* PC);
	void DrawScoreboard(AMFGameState* GS);
	/** RageTeamScoreBoard look: top band with the result, a column per squad, bottom band. */
	void DrawScoreboardMF(AMFGameState* GS);
	void DrawDeadScreen(AMFGameState* GS, AMFPlayerController* PC);

	/** Draw a weapon's icon (or its name if the icon isn't converted). Returns drawn width. */
	float DrawWeaponIcon(const AMFWeapon* Weapon, float X, float Y, float Height, const FLinearColor& Tint);

	void Text(const FString& S, float X, float Y, const FLinearColor& Color, float Scale = 1.f, bool bCenter = false);
	/** MF bitmap font for the current HUD size (RageFontInfo GetHUDSmall/Med/BigFont): 0 small, 1 medium, 2 big. Null if not imported. */
	UFont* MFFont(int32 Kind);
	/** Text in an MF font at its native size (falls back to Text()). */
	void MFText(const FString& S, float X, float Y, const FLinearColor& Color, int32 Kind);
	FVector2D MFTextSize(const FString& S, int32 Kind);
	int32 SizeIndex = 2;
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UFont>> MFFonts;
	void Box(float X, float Y, float W, float H, const FLinearColor& Color);
	void Frame(float X, float Y, float W, float H, const FLinearColor& Fill, const FLinearColor& Border);
	/** One MF inventory box: icon, key in the corner, ammo bar below. */
	void DrawItemCell(const AMFWeapon* W, float X, float Y, float CellW, float CellH, EMFCellState State, const FString& Ammo, const FString& Corner);
	static FString AmmoText(const AMFWeapon* W, bool bLive);

	struct FMessage { FString Text; FLinearColor Color; float Time; };
	TArray<FMessage> Messages;
	FString Announcement;
	float AnnouncementTime = -100.f;
	FString SelectedName;
	float SelectedTime = -100.f;

	float UIScale = 1.f;

	// --- MF art (RageHUD atlases from the original game; plain shapes without them) -----
	bool LoadMFArt();
	void DrawTexRect(UTexture2D* Tex, float X, float Y, float W, float H, float U, float V, float UL, float VL,
		const FLinearColor& Color, ESimpleElementBlendMode Blend);
	/** Box colour set: 0 blue team, 1 red team, 2 white (selected), 3 grey (no room). */
	int32 TeamSet() const;
	FLinearColor SetColour(int32 Set) const;
	void DrawWeapIconMF(const AMFWeapon* W, float X, float Y, int32 Set, const FString& Ammo, const FString& Corner);
	void DrawEmptyMF(int32 Blocks, float X, float Y);
	void DrawInventoryMF(AMFCharacter* C, AMFPlayerController* PC);
	void DrawHealthMF(AMFCharacter* C);
	void DrawLoadoutMF(AMFCharacter* C, AMFPlayerController* PC);
	void DrawPainMF();
	/** RageHUD.DrawMiniMap: map around the player top left, team border, team-mates, objectives. */
	void DrawMiniMapMF(AMFCharacter* C);
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TexMiniIcons;
	UPROPERTY(Transient)
	TObjectPtr<class AMFMapInfo> MapInfo;
	bool bLookedForMapInfo = false;
	/** RageTeamHUD.DrawScoreBar: box in a colour set, fill, icon to the right, text. */
	void DrawScoreBarMF(float X, float Y, float W, float H, int32 BoxSet, int32 Icon, const FString& Label, float Level);
	/** Bottom right: team score panels (RageTeamHUD.DrawTeamScores) or the mode's bars. True if drawn. */
	bool DrawGameInfoMF(AMFGameState* GS);
	float HitTime = -100.f;
	FVector HitDir = FVector::ZeroVector;
	float Block = 64.f;   // MF BlockSize
	float RS = 1.f;       // MF RenderScale = BlockSize / 64
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TexBoxes;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TexBacks;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TexIcons;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TexLoadoutBack[2];

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TexScoreBoxes;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TexScoreIcons;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TexCrosshair[24];
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TexPain[2];
	bool bTriedArt = false;
	UPROPERTY(Transient)
	TObjectPtr<UFont> Font;
	UPROPERTY(Transient)
	TMap<TObjectPtr<UClass>, TObjectPtr<UTexture2D>> IconCache;
};
