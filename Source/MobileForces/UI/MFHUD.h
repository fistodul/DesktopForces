#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MFHUD.generated.h"

class AMFCharacter;
class AMFGameState;
class AMFPlayerController;

/**
 * Canvas HUD (like MF's RageHUD): crosshair, health/armour, ammo, carried weapons,
 * clock and scores, kill feed, announcements, loadout menu and scoreboard.
 * Canvas drawing costs a handful of draw calls and no widget ticking, which suits
 * low-end and mobile targets. Original MF HUD textures can replace the plain shapes later.
 */
UCLASS()
class MOBILEFORCES_API AMFHUD : public AHUD
{
	GENERATED_BODY()

public:
	AMFHUD();
	virtual void DrawHUD() override;

protected:
	void DrawCrosshair(AMFCharacter* C);
	void DrawStatus(AMFCharacter* C);
	void DrawWeaponBar(AMFCharacter* C);
	void DrawGameInfo(AMFGameState* GS, AMFPlayerController* PC);
	void DrawKillFeed(AMFGameState* GS);
	void DrawLoadout(AMFCharacter* C, AMFPlayerController* PC);
	void DrawScoreboard(AMFGameState* GS);
	void DrawDeadScreen(AMFGameState* GS, AMFPlayerController* PC);

	void Text(const FString& S, float X, float Y, const FLinearColor& Color, float Scale = 1.f, bool bCenter = false);
	void Box(float X, float Y, float W, float H, const FLinearColor& Color);

	float UIScale = 1.f;
	UPROPERTY(Transient)
	TObjectPtr<UFont> Font;
};
