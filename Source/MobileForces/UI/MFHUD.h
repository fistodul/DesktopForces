#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MFHUD.generated.h"

class AMFCharacter;
class AMFGameState;
class AMFPlayerController;
class AMFWeapon;
class UTexture2D;

/**
 * Canvas HUD (like MF's RageHUD): crosshair/scope, health/armour, weapon strip with the
 * original icons, clock and team scores, kill feed, announcements, loadout menu, scoreboard.
 * Canvas drawing is a handful of draw calls with no widget tree to tick - good for low-end
 * and mobile. Messages arrive through UMFDeathMessage / UMFAnnouncement.
 */
UCLASS()
class MOBILEFORCES_API AMFHUD : public AHUD
{
	GENERATED_BODY()

public:
	AMFHUD();
	virtual void DrawHUD() override;

	void AddKillMessage(const FString& Killer, const FString& Victim, const FString& Verb);
	void Announce(const FString& Text);
	/** Show the selected weapon's name for a moment (weapon switching). */
	void NotifyWeaponSelected(const AMFWeapon* Weapon);

protected:
	void DrawCrosshair(AMFCharacter* C);
	void DrawStatus(AMFCharacter* C);
	void DrawWeaponBar(AMFCharacter* C);
	void DrawGameInfo(AMFGameState* GS, AMFPlayerController* PC);
	void DrawKillFeed();
	void DrawLoadout(AMFCharacter* C, AMFPlayerController* PC);
	void DrawScoreboard(AMFGameState* GS);
	void DrawDeadScreen(AMFGameState* GS, AMFPlayerController* PC);

	/** Draw a weapon's icon (or its name if the icon isn't converted). Returns drawn width. */
	float DrawWeaponIcon(const AMFWeapon* Weapon, float X, float Y, float Height, const FLinearColor& Tint);

	void Text(const FString& S, float X, float Y, const FLinearColor& Color, float Scale = 1.f, bool bCenter = false);
	void Box(float X, float Y, float W, float H, const FLinearColor& Color);

	struct FKill { FString Killer, Victim, Verb; float Time; };
	TArray<FKill> KillFeed;
	FString Announcement;
	float AnnouncementTime = -100.f;
	FString SelectedName;
	float SelectedTime = -100.f;

	float UIScale = 1.f;
	UPROPERTY(Transient)
	TObjectPtr<UFont> Font;
	UPROPERTY(Transient)
	TMap<TObjectPtr<UClass>, TObjectPtr<UTexture2D>> IconCache;
};
