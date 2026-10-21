#pragma once

#include "CoreMinimal.h"
#include "Game/MFFlag.h"
#include "MFGameMode_Coop.generated.h"

/**
 * Coop mode (MF-mods CoopMode, by Animeman for Akvari): capture the flag, every human on the red
 * team against a blue team of bots. Weapons limits where humans may open the loadout. Maps:
 * COOP-*. Play: ?game=/Script/DesktopForces.MFGameMode_Coop?Bots=12
 */
UCLASS()
class DESKTOPFORCES_API AMFGameMode_Coop : public AMFGameMode_CTF
{
	GENERATED_BODY()

public:
	AMFGameMode_Coop();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void LoadoutDenied(APlayerController* PC) const override;

	/**
	 * MF CoopMode.weapons: 0 no loadout, 1-2 only weapons on the ground, 3 + loadout crates
	 * (default), 4 + loadout zones. URL option ?Weapons=N.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 Weapons = 3;

protected:
	/** Humans red (1), bots blue (0). */
	virtual uint8 ChooseTeam(AController* Who, uint8 Preferred) const override;
};
