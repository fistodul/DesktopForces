#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "MFPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;

/** A key that runs a console command (MF User.ini [Engine.Input] style). */
USTRUCT()
struct FMFCommandBind
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, Category = "MF")
	FKey Key;

	UPROPERTY(Config, EditAnywhere, Category = "MF")
	FString Command;
};
class AMFCharacter;
class AMFWeapon;

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
class DESKTOPFORCES_API AMFPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMFPlayerController();

	AMFCharacter* GetMFCharacter() const;

	// --- loadout (server) ---------------------------------------------------------------
	/** Open the loadout screen for this player (server). */
	void EnterLoadout();
	void ExitLoadout();

	// --- loadout (client UI state, drawn by AMFHUD) ---------------------------------------
	bool IsLoadoutOpen() const;
	/** Items shown in the menu: the source's items, ordered by slot key (menu rows). */
	TArray<TSubclassOf<AMFWeapon>> GetLoadoutItems() const;
	/** Away from crates and loadout zones the loadout offers the weapons lying around
	 *  (MF Loadout_Ground / TryLoadoutDropped, 250 uu). Sorted like GetLoadoutItems. */
	TArray<AMFWeapon*> GetGroundWeapons() const;
	bool IsGroundLoadout() const;
	/** Carried items the cursor can reach to put back (MF LoadoutInventory; never the knife). */
	TArray<AMFWeapon*> GetLoadoutInventory() const;
	/**
	 * MF loadout cursor: 0 .. store-1 = a store item you can take, then the carried items
	 * (selecting one puts it back). W/S, Up/Down and the mouse wheel walk through both.
	 */
	int32 LoadoutCursor = 0;
	/** Carried item under the cursor (cursor in the inventory part), else null. */
	AMFWeapon* GetLoadoutCursorWeapon() const;
	/**
	 * Slot key whose row is waiting for the second key press (0 = none). A row with one
	 * item is picked by its key directly (MF); a row with more opens and the next number
	 * picks the item in it, e.g. 3 then 2 = second item of row 3.
	 */
	int32 GetLoadoutOpenSlot() const;

	UPROPERTY(Config, EditAnywhere, Category = "MF")
	float MouseSensitivity = 1.f;

	/**
	 * Keys that run console commands, like MF's User.ini (F9=SShot, T=Talk, V=ShowCommandMenu
	 * ...). Defaults in DefaultGame.ini; players and mods add or change lines there.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "MF")
	TArray<FMFCommandBind> CommandBindings;

	// --- orders menu (MF OrdersMainWindow / TargetChildWindow), drawn by AMFHUD -----------
	/** 0 closed, 1 choosing the order, 2 choosing who gets it. */
	int32 GetCommandMenuStage() const { return CommandMenuStage; }
	void SetCommandMenuStage(int32 Stage);

	/** MF Tab: the one-line console. */
	UFUNCTION(Exec)
	void Type();
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
	/** Re-read UMFUserSettings (field of view; the menu calls it after Apply). */
	UFUNCTION(Exec)
	void MFApplyUserSettings();

	// --- console commands (also used by automated smoke tests via -MFExec) ---
	/** Take loadout item N (0-based, menu order). */
	UFUNCTION(Exec)
	void MFLoadoutPick(int32 Index);
	/** Leave / enter the loadout screen. */
	UFUNCTION(Exec)
	void MFLoadout();
	/** Select inventory group N (like the number keys). */
	UFUNCTION(Exec)
	void MFSelect(int32 Group);
	/** Scripted movement input (forward, right) for Seconds (MFWalk cheat). */
	void ForceMove(const FVector2D& Input, float Seconds);
	/** Screenshot to Saved/Screenshots/<platform>/<Name>.png */
	UFUNCTION(Exec)
	void MFShot(const FString& Name);
	/** Toggle the scoreboard. */
	UFUNCTION(Exec)
	void MFScores();

protected:
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;

	UFUNCTION(Server, Reliable)
	void ServerToggleLoadout();
	UFUNCTION(Server, Reliable)
	void ServerLoadoutPick(TSubclassOf<AMFWeapon> Item);
	UFUNCTION(Server, Reliable)
	void ServerDropWeapon();
	UFUNCTION(Server, Reliable)
	void ServerLoadoutDrop(AMFWeapon* Item);
	UFUNCTION(Server, Reliable)
	void ServerSuicide();
	UFUNCTION(Server, Reliable)
	void ServerAddBots(int32 N, float SkillOffset);
	UFUNCTION(Server, Reliable)
	void ServerSay(const FString& Msg, bool bTeam);
	UFUNCTION(Server, Reliable)
	void ServerGiveOrder(int32 Order, APlayerState* Target);

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

	// MFWalk: scripted movement input
	FVector2D ForcedMove = FVector2D::ZeroVector;
	float ForcedMoveUntil = 0.f;
	float ForcedMoveNextLog = 0.f;
	int32 LoadoutOpenSlot = 0;
	float LoadoutOpenTime = 0.f;
};
