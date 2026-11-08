#pragma once

#include "CoreMinimal.h"
#include "Game/DFGameMode.h"
#include "OneWeaponGame.generated.h"

class ADFWeapon;

/**
 * Deathmatch with one weapon for everyone (MF-mods KnifeFight and Swars by Liam Haynes, Shotgun
 * Wars modified by Gregaras): no loadout and no items lying around (PreBeginPlay destroyed the
 * crates and InventorySpots), everyone starts with the knife and the mode's weapon, armour too
 * with ?HasArmour=1 (bHasArmour). Killing sprees have the mode's own lines (KFMessages /
 * SWMessages) and every kill gets "Ooooh, blood everywhere now..." (ScoreKill).
 * Not ported: bIceMode (ZoneGroundFriction 0.25) and bDeathSloMo (TimeDilation 0.35), both off.
 */
UCLASS(Abstract, Config = Game)
class DESKTOPFORCES_API AOneWeaponGame : public ADeathMatch {
	GENERATED_BODY()

public:
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void SetPlayerDefaults(APawn* PlayerPawn) override;
	virtual FText GetSpreeNote(int32 Level) const override;
	virtual FName GetSpreeSound(int32 Level) const override;

	/** Body armour from the start (KnifeFightGame.bHasArmour: AddDefaultInventory RageArmour). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	bool bHasArmour = false;

protected:
	virtual void ScoreKill(ADFPlayerState* Killer, ADFPlayerState* Victim) override;

	/** The mode's weapon: the first whose content is installed. */
	TArray<TSubclassOf<ADFWeapon>> Weapons;
	/** Its clips, by weapon (0: the weapon's own). */
	TArray<int32> WeaponClips;
	TArray<FText> SpreeNotes;
	TArray<FName> SpreeSounds;
};

/** Knife Fight (KnifeFight.KnifeFightGame): knives only, 20 kills or 10 minutes. ?game=kf */
UCLASS(Config = Game)
class DESKTOPFORCES_API AKnifeFight : public AOneWeaponGame {
	GENERATED_BODY()
public:
	AKnifeFight();
};

/** Shotgun Wars (Swars.ShotgunWarsGame): the M1887 with 100 clips (m87Swars), 6 minutes; where
 *  the M1887 isn't installed the shotgun with 5000 (SWShotgun). ?game=sw */
UCLASS(Config = Game)
class DESKTOPFORCES_API AShotgunWars : public AOneWeaponGame {
	GENERATED_BODY()
public:
	AShotgunWars();
};
