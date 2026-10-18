#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Core/MFTypes.h"
#include "MFGameMode.generated.h"

class AMFCharacter;
class AMFPlayerState;
class AMFLoadoutCrate;

/**
 * Deathmatch rules (MF RageGameInfo + RageDeathMatch). Base class of every MF game mode.
 *
 *  - Frag limit 30, time limit 7 min (System/MobileForces.ini [Rage.RageDeathMatch]).
 *  - "Hardcore mode": all damage dealt by someone x1.5 (RageDeathMatch.ReduceDamage).
 *  - Kill +1 frag, suicide -1 (GameInfo.ScoreKill).
 *  - Spawn spot: neutral starts only, random score minus nearby players (FindPlayerStart).
 *  - Respawn after max(1, RespawnWait) seconds; players spawning at a loadout crate or
 *    in a loadout zone start in the loadout screen.
 *
 * Map options override config: ?FragLimit=10?TimeLimit=5?GoalTeamScore=3?Bots=4
 */
UCLASS(Config = Game)
class MOBILEFORCES_API AMFGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMFGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void StartPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override { return false; }
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void SetPlayerDefaults(APawn* PlayerPawn) override;
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;

	/** Damage modifiers (hardcore x1.5, friendly fire). */
	virtual float ModifyDamage(float Damage, AMFCharacter* Victim, AController* Instigator) const;
	/** A pawn died. */
	virtual void Killed(AController* Killer, AController* Victim, AMFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType);
	virtual bool IsTeamGame() const { return false; }
	/** Whether actors flagged for team / non-team play exist in this mode (MF Actor.bTeamPlay / bNonTeamPlay). */
	bool IsRelevant(bool bForTeamPlay, bool bForNonTeamPlay) const { return IsTeamGame() ? bForTeamPlay : bForNonTeamPlay; }

	/** Loadout: can this player use the crate / zone they're standing in? */
	virtual bool CanUseLoadout(AMFCharacter* Character) const;

	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 FragLimit = 30;

	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 TimeLimit = 7;

	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 RespawnWait = 0;

	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	bool bHardCoreMode = true;

	/** Seconds on the end screen before the map restarts (MF RestartWait). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 RestartWait = 15;

	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	float FriendlyFireScale = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "MF")
	FString GameName = TEXT("DeathMatch");

	/** Bots added at match start (map option ?Bots=N). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 NumBots = 0;

	/** 0..1 (map option ?BotSkill=0.7). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	float BotSkill = 0.5f;

	/** Spawn bots from the MF roster. Server only. */
	void AddBots(int32 Count);

protected:
	virtual void ScoreKill(AMFPlayerState* Killer, AMFPlayerState* Victim);
	virtual void CheckEndConditions(AMFPlayerState* LastKiller);
	virtual void EndMatch(const FString& WinnerText);
	virtual bool IsStartAllowed(const class AMFPlayerStart* Start, AController* Player) const;
	virtual void OnTimeLimit();
	/** Team for a joining player / bot (team games). */
	virtual void AssignTeam(class AMFPlayerState* PS, EMFTeam Preferred) {}
	int32 BotsAdded = 0;

	void ClockTick();
	void ScheduleRespawn(AController* Victim);
	void RestartMap();

	FTimerHandle ClockTimer;
	TMap<TWeakObjectPtr<AController>, FTimerHandle> RespawnTimers;
	TWeakObjectPtr<AActor> LastStartSpot;
	bool bFirstBlood = false;
};

/** Team deathmatch (MF RageTeamGame): 2 teams, team spawns, no friendly fire, 12 min. */
UCLASS(Config = Game)
class MOBILEFORCES_API AMFGameMode_TDM : public AMFGameMode
{
	GENERATED_BODY()

public:
	AMFGameMode_TDM();

	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual bool IsTeamGame() const override { return true; }
	virtual float ModifyDamage(float Damage, AMFCharacter* Victim, AController* Instigator) const override;

	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 GoalTeamScore = 0;

	/** Put a player on the smaller team. */
	EMFTeam PickTeam() const;

protected:
	virtual void InitGameState() override;
	virtual void ScoreKill(AMFPlayerState* Killer, AMFPlayerState* Victim) override;
	virtual void CheckEndConditions(AMFPlayerState* LastKiller) override;
	virtual bool IsStartAllowed(const class AMFPlayerStart* Start, AController* Player) const override;
	virtual void OnTimeLimit() override;
	virtual void AssignTeam(class AMFPlayerState* PS, EMFTeam Preferred) override;
};

/** Plain alias so maps / URLs can say ?game=DM. */
UCLASS(Config = Game)
class MOBILEFORCES_API AMFGameMode_DM : public AMFGameMode
{
	GENERATED_BODY()
};
