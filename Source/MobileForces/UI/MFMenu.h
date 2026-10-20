#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "MFMenu.generated.h"

class ACameraActor;

/**
 * Main menu and in-game (Esc) menu, plain Slate built in code (no assets): start a game
 * (map, mode, bots, skill, time limit, LAN host), join an address, options (name,
 * resolution, window mode, quality, brightness, mouse, field of view), quit.
 */
namespace MFMenu
{
	/** Show the menu for a local player (bInGame: Resume / Leave instead of the title screen). */
	MOBILEFORCES_API void Open(APlayerController* PC, bool bInGame);
	MOBILEFORCES_API void Close(APlayerController* PC);
	MOBILEFORCES_API bool IsOpen();
	/** Back to the title screen (the default map with the menu game mode). */
	MOBILEFORCES_API void ReturnToMainMenu(APlayerController* PC);
	/** Scripted tests: show page N (0 main, 1 play, 2 join, 3 options) / press Start. */
	MOBILEFORCES_API void DebugPage(int32 Page);
	MOBILEFORCES_API void DebugStart();
}

/**
 * Title screen game mode: no pawns, the menu over a slowly turning view of the map.
 * The project's default map starts with it (DefaultEngine.ini LocalMapOptions).
 */
UCLASS()
class MOBILEFORCES_API AMFMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMFMenuGameMode();
};

UCLASS()
class MOBILEFORCES_API AMFMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMFMenuPlayerController();
	virtual void Tick(float DeltaSeconds) override;

	/** Developer: switch menu page (scripted screenshots). */
	UFUNCTION(Exec)
	void MFMenuPage(int32 Page) { MFMenu::DebugPage(Page); }
	/** Developer: press Start on the play page with its current choices. */
	UFUNCTION(Exec)
	void MFMenuStart() { MFMenu::DebugStart(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> MenuCamera;
};
