#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "MFPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
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
class MOBILEFORCES_API AMFPlayerController : public APlayerController
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
	int32 LoadoutOpenSlot = 0;
	float LoadoutOpenTime = 0.f;
};
