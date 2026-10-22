#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "Core/DFTypes.h"
#include "DFGameMode.generated.h"

class ADFCharacter;
class ADFPlayerState;
class ADFPlayerStart;
class ADFWeapon;

/**
 * Base rules for every Mobile Forces mode (MF RageGameInfo + RageDeathMatch + RageTeamGame
 * folded into one class; UE's AGameMode supplies the match states).
 *
 *  - NumTeams 0 = deathmatch; N > 0 = team game with N teams (MF had 2; any number works,
 *    names/colours from UDFGameSettings::Teams).
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
class DESKTOPFORCES_API ADFGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	ADFGameMode();

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
	/** The mapper's placed RageBots (ADFBotSpot) come alive. */
	void SpawnPlacedBots();
	virtual void HandleMatchHasEnded() override;
	/** After a match's end screen: the next map of MapRotation, else the same one again
	 *  (RestartGame stays a plain restart: the restart vote). */
	virtual void TravelAfterMatch();

	/** Damage modifiers (hardcore x1.5, friendly fire). */
	virtual float ModifyDamage(float Damage, ADFCharacter* Victim, AController* InstigatedBy, AActor* DamageCauser = nullptr) const;
	/** A pawn died. */
	virtual void Killed(AController* Killer, AController* Victim, ADFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType);
	/** A blow would kill Victim: return true to keep them alive (mods: Tug of War converts instead). */
	virtual bool PreventDeath(ADFCharacter* Victim, AController* Killer, TSubclassOf<UDamageType> DamageType) { return false; }

	/** Someone got into a vehicle (Trailer: "trailer has been captured!"). */
	virtual void VehicleEntered(class ADFVehicle* Vehicle, ADFCharacter* Driver) {}
	/** The vehicle a map generator makes for its stock class (?Vehicles=, ?Trucks=, ?APCs=, the
	 *  map's VehicleSwaps). */
	virtual UClass* GetVehicleClassFor(UClass* StockClass) const;

	/** Places where the map's vehicle generators stay off in this mode (Trailer: the delivery bays). */
	virtual void GetVehicleExclusions(TArray<FVector>& OutPoints) const {}

	/** Round-based modes: start the clock again, clear planted / dropped items, respawn everyone. */
	void RestartRound();

	bool IsTeamGame() const { return NumTeams > 0; }
	/** Whether actors flagged for team / non-team play exist in this mode (MF Actor.bTeamPlay / bNonTeamPlay). */
	bool IsRelevant(bool bForTeamPlay, bool bForNonTeamPlay) const { return IsTeamGame() ? bForTeamPlay : bForNonTeamPlay; }

	/** Loadout: can this player use the crate / zone they're standing in? */
	virtual bool CanUseLoadout(ADFCharacter* Character) const;
	/** The weapon a soldier really gets when given WeaponClass (modes with their own variants). */
	virtual TSubclassOf<ADFWeapon> GetWeaponClassFor(const ADFCharacter* Character, TSubclassOf<ADFWeapon> WeaponClass) const { return WeaponClass; }
	/** The loadout key was pressed where it can't open (ADFGameState::LoadoutAccess 0). */
	virtual void LoadoutDenied(APlayerController* PC) const;

	/**
	 * Where a bot with nothing to shoot at should go, best first (objective modes); empty =
	 * roam freely. The bot skips goals it found unreachable recently.
	 */
	virtual void GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const {}

	/** Something that isn't a player but bots should shoot at when no enemy is in sight (e.g. a safe). */
	virtual AActor* GetBotShootTarget(AController* Bot) const { return nullptr; }
	/** Something a driving bot keeps driving at instead of taking the road to its goal and
	 *  getting out (DeBall: the ball). */
	virtual AActor* GetBotDriveTarget(AController* Bot) const { return nullptr; }

	// --- votes (Gregaras' vote mutator: mutate callvote ..., mutate vote y / n) -------------
	/** Seconds a vote stays open (votingDuration). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF|Vote")
	int32 VoteDuration = 30;
	/** A player calls a vote: "travel <map> [mode]", "nextmap", "restart", "addbots <n>",
	 *  "kickbots", "shuffle", "balance". The caller says yes; more than half the players
	 *  saying yes passes it. */
	void CallVote(APlayerController* By, const FString& Args);
	void CastVote(APlayerController* By, bool bYes);

	/** End the match with this winner line (also announced). */
	void FinishMatch(const FString& Winner);

	// --- chat and team orders (MF Say / TeamSay, RageVoicePack orders) -----------------------
	/** Game event line for everyone (MF EventN messages), in ColourTeam's colour (255 = white),
	 *  with the announcer saying Voice (Sounds/Announcer/<Voice>) if set. */
	void Event(const FString& Text, uint8 ColourTeam, FName Voice = NAME_None);
	/** The announcer's order to a player who just joined (InstructionSound): "Kill_them_all",
	 *  "Destroy_the_enemy", "Get_the_flag" ... */
	virtual FName GetInstructionSound(const AController* C) const
	{
		return !InstructionSound.IsNone() ? InstructionSound : FName(IsTeamGame() ? TEXT("Destroy_the_enemy") : TEXT("Kill_them_all"));
	}
	/** The MF game class this mode stands for ("RageDeathMatch", "RageCTF", a mod's own ...):
	 *  map actors with a CondGameType exist only in that one (GameInfo.IsRelevant). */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	FName MFGameClass = TEXT("RageDeathMatch");
	/** This mode's order (none: the deathmatch / team deathmatch one). */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	FName InstructionSound;
	/** A chat line to everyone, or to the sender's team (team games). */
	void BroadcastChat(AController* Sender, const FString& Msg, bool bTeam);
	/** AGameMode's own "Say" exec runs first on a host: send it as the local player's chat. */
	virtual void Say(const FString& Msg) override;
	/** MF orders menu entry 0-4: Defend! / Defend this area. / Attack! / Cover me. / Improvise! */
	static FString GetOrderAbbrev(int32 Order);
	/** The spoken order line; modes rename Defend / Attack (MF OrderString 5-14). */
	virtual FString GetOrderString(int32 Order) const;
	/** Its RagePlayerVoice line (RageVoicePack.SetOrderMessage, OrderSound 0-14). */
	virtual FName GetOrderSound(int32 Order) const;
	/** Order a team-mate (Target) or the whole team (null): message + bots follow it. */
	void GiveOrder(AController* Giver, int32 Order, APlayerState* Target);

	/** Spawn bots from the configured roster (UDFGameSettings::Bots, then numbered extras). */
	/** Add bots from the roster; Skill < 0 = the match's BotSkill (MF AddBots / GoodBot / BadBot). */
	void AddBots(int32 Count, float Skill = -1.f);

	/** Teams playing (0 = free-for-all). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	int32 NumTeams = 0;

	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	int32 FragLimit = 30;

	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	int32 TimeLimit = 7;

	/** Seconds; overrides TimeLimit when > 0 (URL option TimeLimitSecs). */
	int32 TimeLimitSeconds = 0;

	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	int32 GoalTeamScore = 0;

	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	int32 RespawnWait = 0;

	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	bool bHardCoreMode = true;

	/** Vehicles at the map's generators. ?Vehicles=0 none, ?Vehicles=Buggy (a class name) or
	 *  Random: every road generator makes that (AllVehicles). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	bool bVehicles = true;

	/*
	 * The community servers' damage rules (mutPack changeDamage by Gregaras) are the game's:
	 * players' rockets hit soldiers twice as hard, non-explosive hits hurt vehicles (each damage
	 * type's VehicleDamageScale; MF: never), bots hurt vehicles and the people in them a fifth
	 * as much. 1 here: MF's numbers.
	 */
	/** rocketDamageMultToPlayersOnFoot 2 (rockets fired by players). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF|Damage")
	float RocketDamageScaleOnFoot = 2.f;
	/** 1 / botDamageDivToVehicles (5) and 1 / botDamageDivToPlayerVehOccupants (5). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF|Damage")
	float BotVehicleDamageScale = 0.2f;
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF|Damage")
	float BotOccupantDamageScale = 0.2f;

	/**
	 * Admins (MF bAdmin): `AdminLogin <password>` with this password (empty: nobody logs in
	 * remotely; the listen server's host and single player are admins anyway). Admins may use
	 * cheats and `Admin <command>` (any console command on the server: Kick, SwitchMap,
	 * servertravel ...). Game.ini [/Script/DesktopForces.DFGameMode] AdminPassword=...
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF|Admin")
	FString AdminPassword;
	/** Cheats (God, SetTeam, GiveWeapon ...) for every player, not only admins (?Cheats=1). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF|Admin")
	bool bAllowCheats = false;

	virtual bool AllowCheats(APlayerController* P) override;
	bool IsAdmin(const AController* C) const;
	/** Server: log in (true) or say why not. */
	bool AdminLogin(APlayerController* PC, const FString& Password);

	/** Gregaras' parachutes (the community servers' mutPack bCanUseParachutes): hold Jump while
	 *  falling fast and a canopy opens; steer by looking. ?Parachutes=0 off. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	bool bParachutes = true;

	/** Most weapons lying around at once; the oldest disappears beyond it. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	int32 MaxDroppedWeapons = 32;

	/** MF-mods TurboCars: drivers get nitro on the crouch key; on by default like the community
	 *  servers (mutPack replaceVehicles: TurboBuggy, TurboHumvee, TurboArmadillo). ?TurboCars=0 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	bool bTurboCars = true;

	/** Gregaras' OPWeapons, simplified (?OPWeapons=1): clips never run dry (ADFWeapon::
	 *  HasInfiniteAmmo) and shots come OPFireRate times as fast (MF: clips of hundreds, fire
	 *  animations played 10-50 times as fast). Into ADFGameState::bInfiniteAmmo / FireRate. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF|Weapons")
	bool bOPWeapons = false;
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF|Weapons")
	float OPFireRate = 4.f;
	/** Gian's improved rocket launcher instead of MF's (?ImprovedRL=1): lighter, faster rockets
	 *  (ARocketLauncher). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF|Weapons")
	bool bImprovedRL = false;
	/** Weapons loadouts don't hand out, by class name, their subclasses too
	 *  (?NoWeapons=Grenades,RocketLauncher; the FNG mutators NoGrenades, NoRocketLauncher ...). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF|Weapons")
	TArray<FString> NoWeapons;

	/** Every road vehicle generator (buggy, humvee, truck, APC) makes this vehicle instead, by
	 *  class name; Random: one of the four each time (?Vehicles=Buggy; FNG AllBuggys ...
	 *  AllRandom). The trailer cab stays. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	FString AllVehicles;
	/** What the truck generators make instead, by class name (?Trucks=Jeep: MF-mods jeeps with a
	 *  gunner; ?Trucks=Heli: the community servers' mutPack replaceVehicles). Empty: trucks. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	FString Trucks;
	/** World gravity times this (?Gravity=0.2: FNG LowGrav, 200 of MF's 950 uu/s^2). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	float Gravity = 1.f;

	/** What the server tells everyone every ServerMessageInterval seconds, ServerMessageGroup
	 *  lines at a time in turn (FNG ServerAds; Game.ini +ServerMessages=...). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF|Server")
	TArray<FString> ServerMessages;
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF|Server")
	float ServerMessageInterval = 600.f;
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF|Server")
	int32 ServerMessageGroup = 1;

	/** mutPack addVehicles: UDFGameSettings::AddedVehicles on their maps (?AddVehicles=0 off). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	bool bAddVehicles = true;

	/** What the APC (Armadillo) generators make instead, by class name (?APCs=ArmedAPC: MF-mods
	 *  Armed APC with a roof machine gun; ArmedAPCRocket: rockets). Empty: Armadillos. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	FString APCs;

	/** Seconds on the end screen before the map restarts (MF RestartWait). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	int32 RestartWait = 15;

	/** Maps played in turn after a match, each a travel URL ("mf-Sawmill?game=det?TimeLimit=10");
	 *  empty: the map restarts. MF's MapList (the community servers turn it on with FNG
	 *  EnableRotation). Maps not converted here are skipped. Game.ini +MapRotation=... */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF|Server")
	TArray<FString> MapRotation;
	/** Travel to the map after this one in MapRotation (this server's own options, e.g. listen
	 *  and Bots, carried along); false if there is none. */
	bool TravelToNextMap();

	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	float FriendlyFireScale = 0.f;

	/** Kills add to the team score (MF RageTeamGame.bScoreTeamKills; off in objective modes). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	bool bScoreTeamKills = true;

	UPROPERTY(EditDefaultsOnly, Category = "DF")
	FString GameName = TEXT("DeathMatch");

	/** Bots added when the match starts (AGameMode::NumBots counts the ones playing). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	int32 BotCount = 0;

	/** 0..1 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF")
	float BotSkill = 0.5f;

protected:
	virtual void ScoreKill(ADFPlayerState* Killer, ADFPlayerState* Victim);
	virtual void CheckEndConditions(ADFPlayerState* LastKiller);
	virtual void OnTimeLimit();
	virtual bool IsStartAllowed(const ADFPlayerStart* Start, AController* Player) const;
	/** Team for a joining player / bot (smallest team; Preferred breaks ties). */
	virtual uint8 PickTeam(uint8 Preferred = DF::NoTeam) const;
	/** Team for this player or bot; modes that put humans and bots apart override it. */
	virtual uint8 ChooseTeam(AController* Who, uint8 Preferred) const { return PickTeam(Preferred); }
	void EnsureKillZ();

	void CheckTimeLimit();
	void ScheduleRespawn(AController* Victim);

	FTimerHandle TimeLimitTimer;
	// votes
	FString VoteCommand;
	TMap<TWeakObjectPtr<APlayerController>, bool> VoteBallots;
	FTimerHandle VoteTimer;
	float VoteRecentlyEndedUntil = 0.f;
	void SayServerMessages();
	int32 NextServerMessage = 0;
	void CountVotes();
	void EndVote(bool bPassed);
	void RunVote(const FString& Command);
	/** "travel" / "nextmap": the map's package and URL, or empty. */
	FString VoteMapURL(const FString& Map, const FString& Mode) const;
	TMap<TWeakObjectPtr<AController>, FTimerHandle> RespawnTimers;
	TWeakObjectPtr<AActor> LastStartSpot;
	int32 BotsAdded = 0;
	bool bFirstBlood = false;
};

/** Deathmatch: the base rules. */
UCLASS(Config = Game)
class DESKTOPFORCES_API ADeathMatch : public ADFGameMode
{
	GENERATED_BODY()
};

/** Team deathmatch (MF RageTeamGame): 2 teams by default (?Teams=N), 12 min, no friendly fire. */
UCLASS(Config = Game)
class DESKTOPFORCES_API ATeamDeathMatch : public ADFGameMode
{
	GENERATED_BODY()
public:
	ATeamDeathMatch();
};
