#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "DFPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;

/** A key that runs a console command (MF User.ini [Engine.Input] style). */
USTRUCT()
struct FDFCommandBind
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, Category = "DF")
	FKey Key;

	UPROPERTY(Config, EditAnywhere, Category = "DF")
	FString Command;
};
class ADFCharacter;
class ADFWeapon;

/** A key the player can change (Options > Controls): Action names an input action or Cmd<N>. */
struct FDFBinding
{
	FName Action;
	FText Label;
	FKey Default;
	/** The second key MF's DefUser.ini gives the same thing (Ctrl and C = Duck); none: unbound. */
	FKey AltDefault;
};

/**
 * Player input + loadout screen flow.
 *
 * Input actions and the default key map (the original MF controls from the manual)
 * are created in code so the project runs without any binary input assets.
 *
 * Loadout (RagePlayer loadout states): standing at a crate / in your team's loadout
 * room, the loadout key opens the menu; picking an item you carry drops it, picking a
 * new one adds it if it fits in your 6 carry blocks. Entering loadout refills ammo.
 * Away from loadout, the loadout key drops the held weapon.
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API ADFPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ADFPlayerController();

	ADFCharacter* GetMFCharacter() const;

	// --- loadout (server) ---------------------------------------------------------------
	/** Open the loadout screen for this player (server). */
	void EnterLoadout();
	void ExitLoadout();

	// --- loadout (client UI state, drawn by ADFHUD) ---------------------------------------
	bool IsLoadoutOpen() const;
	/** Items shown in the menu: the source's items, ordered by slot key (menu rows). */
	TArray<TSubclassOf<ADFWeapon>> GetLoadoutItems() const;
	/** Away from crates and loadout zones the loadout offers the weapons lying around
	 *  (MF Loadout_Ground / TryLoadoutDropped, 250 uu). Sorted like GetLoadoutItems. */
	TArray<ADFWeapon*> GetGroundWeapons() const;
	bool IsGroundLoadout() const;
	/** Carried items the cursor can reach to put back (MF LoadoutInventory; never the knife). */
	TArray<ADFWeapon*> GetLoadoutInventory() const;
	/**
	 * MF loadout cursor: 0 .. store-1 = a store item you can take, then the carried items
	 * (selecting one puts it back). W/S, Up/Down and the mouse wheel walk through both.
	 */
	int32 LoadoutCursor = 0;
	/** Carried item under the cursor (cursor in the inventory part), else null. */
	ADFWeapon* GetLoadoutCursorWeapon() const;
	/**
	 * Slot key whose row is waiting for the second key press (0 = none). A row with one
	 * item is picked by its key directly (MF); a row with more opens and the next number
	 * picks the item in it, e.g. 3 then 2 = second item of row 3.
	 */
	int32 GetLoadoutOpenSlot() const;

	UPROPERTY(Config, EditAnywhere, Category = "DF")
	float MouseSensitivity = 1.f;

	/**
	 * Keys that run console commands, like MF's User.ini (F9=SShot, T=Talk, V=ShowCommandMenu
	 * ...). Defaults in DefaultGame.ini; players and mods add or change lines there.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "DF")
	TArray<FDFCommandBind> CommandBindings;

	// --- orders menu (MF OrdersMainWindow / TargetChildWindow), drawn by ADFHUD -----------
	/** 0 closed, 1 choosing the order, 2 choosing who gets it. */
	int32 GetCommandMenuStage() const { return CommandMenuStage; }
	void SetCommandMenuStage(int32 Stage);

	/** MF Tab: the one-line console. */
	UFUNCTION(Exec)
	void Type();

	/** UE1 PlayerPawn.BehindView: the camera behind you (1), in your eyes (0), or the other one. */
	UFUNCTION(Exec)
	void BehindView(int32 On = -1);

	/** MF / UT `Name <new name>`: rename yourself (kept as the name in Options too). */
	UFUNCTION(Exec)
	void Name(const FString& NewName);
	bool IsBehindView() const { return bBehindView; }

	/** Admin (MF AdminLogin / AdminLogout / Admin): ADFGameMode::AdminPassword. */
	UFUNCTION(Exec)
	void AdminLogin(const FString& Password);
	UFUNCTION(Exec)
	void AdminLogout();
	/** Run a console command on the server: cheats if allowed; admins: any command. */
	UFUNCTION(Exec)
	void Admin(const FString& Command);
	/** The one server door for all of these (UE won't let an Exec function be an RPC):
	 *  "AdminLogin <password>", "AdminLogout", or a command to run (forwarded cheats too). */
	UFUNCTION(Server, Reliable)
	void ServerAdmin(const FString& Command);
	/** Cheats may run here (single player, admin, a ?Cheats=1 server); client side, for show. */
	bool CanCheat() const;

	/** Votes (Gregaras' vote mutator): "CallVote travel mf-warehouse ctf", "Vote yes" / "Vote no". */
	UFUNCTION(Exec)
	void CallVote(const FString& Args);
	UFUNCTION(Exec)
	void Vote(const FString& Choice);
	/** The MF way to say it: "mutate callvote ...", "mutate vote y" / "mutate vote n". */
	UFUNCTION(Exec)
	void Mutate(const FString& Args);
	int32 GetCommandMenuOrder() const { return CommandMenuOrder; }
	/** Orders offered in this game type (MF RageVoicePack.GetOrderString 0-4). */
	TArray<FString> GetCommandMenuOrders() const;
	/** Team-mates that can be given an order (after "All"). */
	TArray<APlayerState*> GetCommandTargets() const;

	bool IsScoreboardShown() const { return bShowScores; }

	// --- Mobile Forces console commands (same names as the original game) ---
	/** Kill yourself and respawn (stuck somewhere, wrong spot ...). */
	UFUNCTION(Exec)
	void Suicide();
	UFUNCTION(Exec)
	void TryLoadout();
	UFUNCTION(Exec)
	void LoadoutNext();
	UFUNCTION(Exec)
	void LoadoutPrev();
	UFUNCTION(Exec)
	void LoadoutSelect();
	/** Select inventory group: 1-9, 10 = key 0, 11 = X. */
	UFUNCTION(Exec)
	void SwitchWeapon(int32 Group);
	UFUNCTION(Exec)
	void NextWeapon();
	UFUNCTION(Exec)
	void PrevWeapon();
	/** Drop the held weapon (MF ThrowWeapon / Drop). */
	UFUNCTION(Exec)
	void ThrowWeapon();
	UFUNCTION(Exec)
	void Drop();
	UFUNCTION(Exec)
	void ShowScores();
	UFUNCTION(Exec)
	void SShot();
	/** Add bots (host or single player only): normal, good, bad skill. */
	UFUNCTION(Exec)
	void AddBots(int32 N);
	UFUNCTION(Exec)
	void GoodBot(int32 N);
	UFUNCTION(Exec)
	void BadBot(int32 N);
	UFUNCTION(Exec)
	void ListBots();
	/** HUD size 0-4 (MF HUDSize: 32, 48, 64, 96, 128 px blocks). */
	UFUNCTION(Exec)
	void GrowHUD();
	UFUNCTION(Exec)
	void ShrinkHUD();
	UFUNCTION(Exec)
	void ChangeCrosshair();
	/** Open / close the orders menu (MF V; C here too). Number keys pick. */
	UFUNCTION(Exec)
	void ShowCommandMenu();
	/** Chat: type a line to everyone / your team (MF T / Y). */
	UFUNCTION(Exec)
	void Talk();
	UFUNCTION(Exec)
	void TeamTalk();
	UFUNCTION(Exec)
	void Say(const FString& Msg);
	UFUNCTION(Exec)
	void TeamSay(const FString& Msg);
	/** Watch the next team-mate (MF F5 ViewTeam); ViewPlayerNum N = the Nth (MF NumPad1-8). */
	UFUNCTION(Exec)
	void ViewTeam();
	UFUNCTION(Exec)
	void ViewPlayerNum(int32 N);
	/** Level the view (MF End). */
	UFUNCTION(Exec)
	void CenterView();
	/** MF TryEnterExit (E): get into the nearest vehicle / out of this one. */
	UFUNCTION(Exec)
	void TryEnterExit();
	UFUNCTION(Server, Reliable)
	void ServerTryEnterExit();
	/** MF Reload (R). */
	UFUNCTION(Exec)
	void Reload() { OnReload(); }
	/** MF TossFlag (G): drop the flag / gold / key you carry. */
	UFUNCTION(Exec)
	void TossFlag();
	UFUNCTION(Server, Reliable)
	void ServerTossFlag();
	/** MF ShowMenu (Esc): the in-game menu (resume, new game, options, leave, quit). */
	UFUNCTION(Exec)
	void ShowMenu();
	/** MF Brightness: set (0.25 - 3) or step through 0.5 ... 2 with no value. */
	UFUNCTION(Exec)
	void Brightness(float Value = 0.f);

	/** 3D resolution scale 0.25 - 2 (1 = window resolution); saved like the menu option. */
	UFUNCTION(Exec)
	void RenderScale(float Value);
	/** Re-read UDFUserSettings (field of view; the menu calls it after Apply). */
	UFUNCTION(Exec)
	void DFApplyUserSettings();

	// --- console commands (also used by automated smoke tests via -DFExec) ---
	/** Take loadout item N (0-based, menu order). */
	UFUNCTION(Exec)
	void DFLoadoutPick(int32 Index);
	/** Leave / enter the loadout screen. */
	UFUNCTION(Exec)
	void DFLoadout();
	/** Select inventory group N (like the number keys). */
	UFUNCTION(Exec)
	void DFSelect(int32 Group);
	/** Scripted movement input (forward, right) for Seconds (DFWalk cheat). */
	void ForceMove(const FVector2D& Input, float Seconds, float Up = 0.f);
	/** Screenshot to Saved/Screenshots/<platform>/<Name>.png */
	UFUNCTION(Exec)
	void DFShot(const FString& Name);
	/** Toggle the scoreboard. */
	UFUNCTION(Exec)
	void DFScores();

	/** The announcer says Line (Sounds/Announcer/<Line>), cutting off what he was saying
	 *  (RPlayer.ClientPlaySound with bInterrupt). */
	UFUNCTION(Client, Reliable)
	void ClientAnnouncer(FName Line);
	void PlayAnnouncer(FName Line);
	/** RageDeathMatch.GetTimeRemainingSound: "You've got N minutes" for the time left. */
	static FName TimeRemainingLine(int32 Seconds);

protected:
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	TWeakObjectPtr<class UAudioComponent> AnnouncerAudio;
	/** Move keys while driving (sent each frame in PlayerTick). */
	FVector2D DriveMove = FVector2D::ZeroVector;
	void OnMoveStop(const struct FInputActionValue& Value);
	/** RageHUD spoken countdown: last remaining second told. */
	int32 LastReportedTime = -1;
	void TellTime();
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void AcknowledgePossession(APawn* P) override;
	/** Black from the map's start until the first spawn (no glimpse of the world half streamed in). */
	void FadeIn();
	void FadeInSoon(APawn* P);
	bool bFadedIn = false;
	FTimerHandle FadeInTimer;

	UFUNCTION(Server, Reliable)
	void ServerToggleLoadout();
	UFUNCTION(Server, Reliable)
	void ServerLoadoutPick(TSubclassOf<ADFWeapon> Item);
	UFUNCTION(Server, Reliable)
	void ServerDropWeapon();
	UFUNCTION(Server, Reliable)
	void ServerLoadoutDrop(ADFWeapon* Item);
	UFUNCTION(Server, Reliable)
	void ServerSuicide();
	UFUNCTION(Server, Reliable)
	void ServerAddBots(int32 N, float SkillOffset);
	UFUNCTION(Server, Reliable)
	void ServerSay(const FString& Msg, bool bTeam);
	UFUNCTION(Server, Reliable)
	void ServerGiveOrder(int32 Order, APlayerState* Target);
	UFUNCTION(Server, Reliable)
	void ServerCallVote(const FString& Args);
	UFUNCTION(Server, Reliable)
	void ServerVote(bool bYes);

	virtual void ClientMessage_Implementation(const FString& S, FName Type, float MsgLifeTime) override;
	virtual void ClientTeamMessage_Implementation(APlayerState* SenderPlayerState, const FString& S, FName Type, float MsgLifeTime) override;

	int32 CommandMenuStage = 0;
	int32 CommandMenuOrder = 0;
	void CommandMenuKey(int32 Slot);
	/** Watched team-mate index for ViewTeam (-1 = yourself). */
	int32 ViewedTeammate = -1;
	void ViewTeammate(int32 Index);
	void StartTyping(const TCHAR* Prefix);

	// input handlers
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnJump();
	void OnJumpReleased();
	void OnCrouch();
	void OnCrouchReleased();
	void OnWalk();
	void OnWalkReleased();
	void OnFire();
	void OnFireReleased();
	void OnAltFire();
	void OnAltFireReleased();
	void OnReload();
	void OnLoadout();
	void OnNextWeapon();
	void OnPrevWeapon();
	void OnWeaponSlot(int32 Slot);
	void OnScores();
	void OnScoresReleased();
	void OnMenuUp();
	void OnMenuDown();
	void OnMenuSelect();

	void BuildInput();
	/** Keys into MappingContext: MF's defaults, the player's changes (UDFUserSettings::KeyBindings). */
	void MapKeys();

public:
	/** The keys a player may change, MF defaults (DefUser.ini / Manual.pdf) first. */
	static TArray<FDFBinding> GetBindings();
	/** The key an action has now (the player's or the default). */
	static FKey GetBoundKey(const FDFBinding& B, bool bAlt = false);
	/** Where an action's second key is kept in the settings ("Crouch" -> "Crouch_Alt"). */
	static FName AltSlot(FName Action) { return FName(*(Action.ToString() + TEXT("_Alt"))); }
	/** Re-read the player's keys (after Options > Controls). */
	void ApplyKeyBindings();

protected:

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UInputAction>> Actions;

	bool bShowScores = false;

	void LoadoutKey(int32 Slot);
	bool IsLoadoutEntryValid(int32 Index) const;
	void LoadoutMove(int32 Dir);
	void PlayUISound(const TCHAR* Path);
	bool bLoadoutWasOpen = false;
	bool bVehicleHandbrake = false;

	// DFWalk: scripted movement input
	FVector2D ForcedMove = FVector2D::ZeroVector;
	/** Up > 0: Jump held (soldiers jump; handbrake / climb), < 0: Crouch held (nitro / descend). */
	/** BehindView: this machine's choice, kept across respawns. */
	bool bBehindView = false;
	float ForcedUp = 0.f;
	float ForcedMoveUntil = 0.f;
	float ForcedMoveNextLog = 0.f;
	int32 LoadoutOpenSlot = 0;
	float LoadoutOpenTime = 0.f;
};
