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
	int32 LoadoutCursor = 0;
	/**
	 * Slot key whose row is waiting for the second key press (0 = none). A row with one
	 * item is picked by its key directly (MF); a row with more opens and the next number
	 * picks the item in it, e.g. 3 then 2 = second item of row 3.
	 */
	int32 GetLoadoutOpenSlot() const;

	UPROPERTY(Config, EditAnywhere, Category = "MF")
	float MouseSensitivity = 1.f;

	bool IsScoreboardShown() const { return bShowScores; }

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
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;

	UFUNCTION(Server, Reliable)
	void ServerToggleLoadout();
	UFUNCTION(Server, Reliable)
	void ServerLoadoutPick(TSubclassOf<AMFWeapon> Item);
	UFUNCTION(Server, Reliable)
	void ServerDropWeapon();

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
	int32 LoadoutOpenSlot = 0;
	float LoadoutOpenTime = 0.f;
};
