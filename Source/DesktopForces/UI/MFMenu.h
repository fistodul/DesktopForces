#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "MFMenu.generated.h"

/**
 * Main menu and in-game (Esc) menu, plain Slate built in code: start a game (map, mode,
 * bots, skill, time limit, LAN host), join an address, options (name, resolution, window
 * mode, quality, render scale, brightness, mouse, field of view), credits, quit.
 */
namespace MFMenu
{
	/** Show the menu for a local player (bInGame: Resume / Leave instead of the title screen). */
	DESKTOPFORCES_API void Open(APlayerController* PC, bool bInGame);
	DESKTOPFORCES_API void Close(APlayerController* PC);
	DESKTOPFORCES_API bool IsOpen();
	/** Back to the title screen (the default map with the menu game mode). */
	DESKTOPFORCES_API void ReturnToMainMenu(APlayerController* PC);
	/** Scripted tests: show page N (0 main, 1 play, 2 join, 3 options, 4 credits) / press Start. */
	DESKTOPFORCES_API void DebugPage(int32 Page);
	DESKTOPFORCES_API void DebugStart();
}

/**
 * Title screen game mode: no pawns, the menu over a picture (UMFGameSettings::MenuBackgrounds),
 * on the empty default map. The project's default map starts with it (DefaultEngine.ini
 * LocalMapOptions).
 */
UCLASS()
class DESKTOPFORCES_API AMFMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMFMenuGameMode();
};

UCLASS()
class DESKTOPFORCES_API AMFMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMFMenuPlayerController();

	/** Developer: switch menu page (scripted screenshots). */
	UFUNCTION(Exec)
	void MFMenuPage(int32 Page) { MFMenu::DebugPage(Page); }
	/** Developer: press Start on the play page with its current choices. */
	UFUNCTION(Exec)
	void MFMenuStart() { MFMenu::DebugStart(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
};
