#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "Core/MFTypes.h"
#include "MFGameMode.generated.h"

class AMFCharacter;
class AMFPlayerState;
class AMFPlayerStart;
class AMFWeapon;

/**
 * Base rules for every Mobile Forces mode (MF RageGameInfo + RageDeathMatch + RageTeamGame
 * folded into one class; UE's AGameMode supplies the match states).
 *
 *  - NumTeams 0 = deathmatch; N > 0 = team game with N teams (MF had 2; any number works,
 *    names/colours from UMFGameSettings::Teams).
 *  - "Hardcore mode": all damage dealt by someone x1.5 (RageDeathMatch.ReduceDamage).
 *  - Kill +1 frag, suicide -1; team games also score teams (RageTeamGame.ScoreKill).
 *  - Spawns: neutral PlayerStarts in deathmatch, own team's in team games (FindPlayerStart).
 *  - Respawn after max(1, RespawnWait) s; spawning at a loadout crate/zone opens loadout.
 *
 * Game modes extend this by overriding the small virtuals (ScoreKill, CheckEndConditions,
 * OnTimeLimit, IsStartAllowed, SetPlayerDefaults) rather than copying bodies.
 * URL options: ?FragLimit=10?TimeLimit=5?GoalTeamScore=3?Teams=4?Bots=8?BotSkill=0.7?Vehicles=0
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API AMFGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	AMFGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override { return false; }
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;
	virtual void SetPlayerDefaults(APawn* PlayerPawn) override;
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;
	virtual void HandleMatchHasStarted() override;
	virtual void HandleMatchHasEnded() override;

	/** Damage modifiers (hardcore x1.5, friendly fire). */
	virtual float ModifyDamage(float Damage, AMFCharacter* Victim, AController* Instigator, AActor* DamageCauser = nullptr) const;
	/** A pawn died. */
	virtual void Killed(AController* Killer, AController* Victim, AMFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType);
	/** A blow would kill Victim: return true to keep them alive (mods: Tug of War converts instead). */
	virtual bool PreventDeath(AMFCharacter* Victim, AController* Killer, TSubclassOf<UDamageType> DamageType) { return false; }

	/** Someone got into a vehicle (Trailer: "trailer has been captured!"). */
	virtual void VehicleEntered(class AMFVehicle* Vehicle, AMFCharacter* Driver) {}
	/** The vehicle a map generator makes for its stock class (URL ?ArmedAPC=1 / 2: the MF-mods
	 *  Armed APC instead of the Armadillo). */
	virtual UClass* GetVehicleClassFor(UClass* StockClass) const;

	/** Places where the map's vehicle generators stay off in this mode (Trailer: the delivery bays). */
	virtual void GetVehicleExclusions(TArray<FVector>& OutPoints) const {}

	/** Round-based modes: start the clock again, clear planted / dropped items, respawn everyone. */
	void RestartRound();

	bool IsTeamGame() const { return NumTeams > 0; }
	/** Whether actors flagged for team / non-team play exist in this mode (MF Actor.bTeamPlay / bNonTeamPlay). */
	bool IsRelevant(bool bForTeamPlay, bool bForNonTeamPlay) const { return IsTeamGame() ? bForTeamPlay : bForNonTeamPlay; }

	/** Loadout: can this player use the crate / zone they're standing in? */
	virtual bool CanUseLoadout(AMFCharacter* Character) const;
	/** The weapon a soldier really gets when given WeaponClass (modes with their own variants). */
	virtual TSubclassOf<AMFWeapon> GetWeaponClassFor(const AMFCharacter* Character, TSubclassOf<AMFWeapon> WeaponClass) const { return WeaponClass; }
	/** The loadout key was pressed where it can't open (AMFGameState::LoadoutAccess 0). */
	virtual void LoadoutDenied(APlayerController* PC) const;

	/**
	 * Where a bot with nothing to shoot at should go, best first (objective modes); empty =
	 * roam freely. The bot skips goals it found unreachable recently.
	 */
	virtual void GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const {}

	/** Something that isn't a player but bots should shoot at when no enemy is in sight (e.g. a safe). */
	virtual AActor* GetBotShootTarget(AController* Bot) const { return nullptr; }

	/** End the match with this winner line (also announced). */
	void FinishMatch(const FString& Winner);

	// --- chat and team orders (MF Say / TeamSay, RageVoicePack orders) -----------------------
	/** Game event line for everyone (MF EventN messages), in ColourTeam's colour (255 = white). */
	void Event(const FString& Text, uint8 ColourTeam);
	/** A chat line to everyone, or to the sender's team (team games). */
	void BroadcastChat(AController* Sender, const FString& Msg, bool bTeam);
	/** AGameMode's own "Say" exec runs first on a host: send it as the local player's chat. */
	virtual void Say(const FString& Msg) override;
	/** MF orders menu entry 0-4: Defend! / Defend this area. / Attack! / Cover me. / Improvise! */
	static FString GetOrderAbbrev(int32 Order);
	/** The spoken order line; modes rename Defend / Attack (MF OrderString 5-14). */
	virtual FString GetOrderString(int32 Order) const;
	/** Order a team-mate (Target) or the whole team (null): message + bots follow it. */
	void GiveOrder(AController* Giver, int32 Order, APlayerState* Target);

	/** Spawn bots from the configured roster (UMFGameSettings::Bots, then numbered extras). */
	/** Add bots from the roster; Skill < 0 = the match's BotSkill (MF AddBots / GoodBot / BadBot). */
	void AddBots(int32 Count, float Skill = -1.f);

	/** Teams playing (0 = free-for-all). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 NumTeams = 0;

	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 FragLimit = 30;

	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 TimeLimit = 7;

	/** Seconds; overrides TimeLimit when > 0 (URL option TimeLimitSecs). */
	int32 TimeLimitSeconds = 0;

	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 GoalTeamScore = 0;

	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 RespawnWait = 0;

	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	bool bHardCoreMode = true;

	/** Vehicles at the map's generators (URL ?Vehicles=0 turns them off). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	bool bVehicles = true;

	/**
	 * mutPack changeDamage (Gregaras), the community servers' damage rules, on by default:
	 * players' rockets hit soldiers twice as hard, bullets and pellets hurt vehicles (MF: never),
	 * bots hurt vehicles and the people in them a fifth as much. ?ChangeDamage=0: MF's rules.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF|Damage")
	bool bChangeDamage = true;
	/** rocketDamageMultToPlayersOnFoot 2 (rockets fired by players). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF|Damage")
	float RocketDamageScaleOnFoot = 2.f;
	/** Bullets / pellets on vehicles, x (1 - Armour): nEWeaponDamageMultToVehicles 10 against
	 *  vehicle health 100 x 8 x 10 (eWeaponDamageMultToVehicles keeps explosives as they were). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF|Damage")
	float VehicleBulletDamageScale = 0.125f;
	/** knivesDamageVehicles */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF|Damage")
	bool bKnivesDamageVehicles = false;
	/** 1 / botDamageDivToVehicles (5) and 1 / botDamageDivToPlayerVehOccupants (5). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF|Damage")
	float BotVehicleDamageScale = 0.2f;
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF|Damage")
	float BotOccupantDamageScale = 0.2f;

	/** Most weapons lying around at once; the oldest disappears beyond it. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 MaxDroppedWeapons = 32;

	/** MF-mods TurboCars: drivers get nitro on the crouch key; on by default like the community
	 *  servers (mutPack replaceVehicles: TurboBuggy, TurboHumvee, TurboArmadillo). ?TurboCars=0 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	bool bTurboCars = true;

	/** MF-mods jeeps (gunner seat) from the truck generators instead of trucks (?Jeeps=1). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	bool bJeeps = false;

	/** MF-mods Armed APC at the Armadillo generators: 0 off, 1 machine gun, 2 rockets (?ArmedAPC=N). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 ArmedAPC = 0;

	/** Seconds on the end screen before the map restarts (MF RestartWait). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 RestartWait = 15;

	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	float FriendlyFireScale = 0.f;

	/** Kills add to the team score (MF RageTeamGame.bScoreTeamKills; off in objective modes). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	bool bScoreTeamKills = true;

	UPROPERTY(EditDefaultsOnly, Category = "MF")
	FString GameName = TEXT("DeathMatch");

	/** Bots added when the match starts (AGameMode::NumBots counts the ones playing). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 BotCount = 0;

	/** 0..1 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	float BotSkill = 0.5f;

protected:
	virtual void ScoreKill(AMFPlayerState* Killer, AMFPlayerState* Victim);
	virtual void CheckEndConditions(AMFPlayerState* LastKiller);
	virtual void OnTimeLimit();
	virtual bool IsStartAllowed(const AMFPlayerStart* Start, AController* Player) const;
	/** Team for a joining player / bot (smallest team; Preferred breaks ties). */
	virtual uint8 PickTeam(uint8 Preferred = MF::NoTeam) const;
	/** Team for this player or bot; modes that put humans and bots apart override it. */
	virtual uint8 ChooseTeam(AController* Who, uint8 Preferred) const { return PickTeam(Preferred); }
	void EnsureKillZ();

	void CheckTimeLimit();
	void ScheduleRespawn(AController* Victim);

	FTimerHandle TimeLimitTimer;
	TMap<TWeakObjectPtr<AController>, FTimerHandle> RespawnTimers;
	TWeakObjectPtr<AActor> LastStartSpot;
	int32 BotsAdded = 0;
	bool bFirstBlood = false;
};

/** Deathmatch: the base rules. */
UCLASS(Config = Game)
class DESKTOPFORCES_API AMFGameMode_DM : public AMFGameMode
{
	GENERATED_BODY()
};

/** Team deathmatch (MF RageTeamGame): 2 teams by default (?Teams=N), 12 min, no friendly fire. */
UCLASS(Config = Game)
class DESKTOPFORCES_API AMFGameMode_TDM : public AMFGameMode
{
	GENERATED_BODY()
public:
	AMFGameMode_TDM();
};
