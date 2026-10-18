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
	/** Items shown in the menu: the source's items. */
	TArray<TSubclassOf<AMFWeapon>> GetLoadoutItems() const;
	int32 LoadoutCursor = 0;

	UPROPERTY(Config, EditAnywhere, Category = "MF")
	float MouseSensitivity = 1.f;

	bool IsScoreboardShown() const { return bShowScores; }

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
};
