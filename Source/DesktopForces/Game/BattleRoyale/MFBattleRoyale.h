#pragma once

#include "CoreMinimal.h"
#include "Game/MFGameMode.h"
#include "MFBattleRoyale.generated.h"

class AMFBRSafeZone;
class AMFWeapon;

/**
 * Battle Royale (MF-mods battleRoyale by Gregaras). Free for all without respawning: everyone
 * drops in from the air at random spots with 500 health and only their default kit, loots item
 * boxes (five random weapons each), shoots metal towers for builder ammo and puts up platforms
 * with the builder. A safe zone shrinks around the map's domination timer; outside it costs
 * health every second. The last one standing ends the round; kills score (or survived rounds,
 * bScoreLastedRounds) up to FragLimit 30 within 12 minutes. Maps: BR-*.
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API AMFGameMode_BattleRoyale : public AMFGameMode
{
	GENERATED_BODY()

public:
	AMFGameMode_BattleRoyale();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void HandleMatchHasStarted() override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void SetPlayerDefaults(APawn* PlayerPawn) override;
	virtual void Killed(AController* Killer, AController* Victim, AMFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType) override;
	virtual void Logout(AController* Exiting) override;
	virtual bool CanUseLoadout(AMFCharacter* Character) const override { return false; }
	virtual void GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const override;

	/** A random item box weapon. */
	UClass* PickItem() const;

	/** What item boxes hand out (battleRoyale weapPackage / weapClass). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "BattleRoyale")
	TArray<TSoftClassPtr<AMFWeapon>> Items;

	/** Out until the round ends once dead (disableRespawn; off on the community servers). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "BattleRoyale")
	bool bDisableRespawn = false;

	/** Scatter spots: every ItemBoxEvery-th is an item box, the rest metal towers (gameItemCount, itemBoxSpawningFreq + 1). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "BattleRoyale")
	int32 GameItemCount = 200;
	UPROPERTY(Config, EditDefaultsOnly, Category = "BattleRoyale")
	int32 ItemBoxEvery = 4;
	/** No two spots closer than this (cm); small maps get fewer. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "BattleRoyale")
	float ItemSpacing = 900.f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "BattleRoyale")
	bool bEnableSafeZone = true;
	/** cm (servers: zoneSize 80000 uu across; minZoneSize 1000). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "BattleRoyale")
	float SafeZoneRadius = 90000.f;
	UPROPERTY(Config, EditDefaultsOnly, Category = "BattleRoyale")
	float MinSafeZoneRadius = 1125.f;
	/** cm/s off the radius (safeZoneShrinkRate 2 uu a tick across). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "BattleRoyale")
	float SafeZoneShrinkRate = 67.5f;
	UPROPERTY(Config, EditDefaultsOnly, Category = "BattleRoyale")
	int32 SafeZoneOutsideDamage = 5;

	/** Health everyone starts with (changeHealth / newHealth). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "BattleRoyale")
	int32 StartHealth = 500;

	/** Drop in from the air (airSpawn) instead of starting on the ground. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "BattleRoyale")
	bool bAirSpawn = true;

	/** Score survived rounds instead of kills (scoreLastedRounds). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "BattleRoyale")
	bool bScoreLastedRounds = false;

protected:
	virtual void ScoreKill(AMFPlayerState* Killer, AMFPlayerState* Victim) override;
	void SpawnItems();
	void ClearItems();
	void NextRound();
	int32 AliveCount(AMFCharacter** OutLast = nullptr) const;
	bool RandomSpot(FVector& Out) const;

	UPROPERTY(Transient)
	TObjectPtr<AMFBRSafeZone> SafeZone;
	TSet<TWeakObjectPtr<AController>> DeadThisRound;
	FBox Playable = FBox(ForceInit);
	TArray<FVector> Starts;
	bool bRoundOver = false;
	bool bRestartingRound = false;
	FTimerHandle RoundTimer;
};
