#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GenericTeamAgentInterface.h"
#include "Core/MFTypes.h"
#include "MFCharacter.generated.h"

class AMFWeapon;
class UCameraComponent;
class UMFCharacterMovement;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMFOnInventoryChanged, AMFCharacter*, Character);

/**
 * The Mobile Forces soldier (MF: Pawn > PlayerPawn > RPlayer > RagePlayer).
 *
 * - First-person camera at MF eye height (BaseEyeHeight 27 above the capsule centre).
 * - Health 100, damage reduced by carried armour items (RageArmour).
 * - Inventory limited by carry blocks (Pawn.MaxCarry = 6); every block carried costs
 *   5% ground speed, down to 50% (Pawn.SetSpeedFromInventory).
 * - Fall damage per Pawn.TakeFallingDamage (threshold -750, death -1200 uu/s).
 * - While in the loadout screen the player is not "in the level" for anyone else:
 *   hidden, no collision, can't be damaged.
 */
UCLASS(Config = Game)
class MOBILEFORCES_API AMFCharacter : public ACharacter, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	AMFCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Destroyed() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void PawnClientRestart() override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void FellOutOfWorld(const UDamageType& DmgType) override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

	UMFCharacterMovement* GetMFMovement() const;
	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }
	USceneComponent* GetWeaponAttachPoint() const { return WeaponAttach; }

	// --- health ---------------------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	int32 MaxHealth = 100;

	UFUNCTION(BlueprintPure, Category = "MF")
	int32 GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "MF")
	bool IsAlive() const { return Health > 0 && !bDying; }

	/** Total armour charge of carried armour items (HUD). */
	UFUNCTION(BlueprintPure, Category = "MF")
	int32 GetArmourCharge() const;

	/** Restore health (AdrenalineShot). Server only. */
	void Heal(int32 Amount);

	/** MF's headshot rule: hit point above 80% of the collision half-height (Pistol/Rifle.ProcessTraceHit). */
	bool IsHeadshot(const FVector& HitLocation) const;

	// --- inventory ------------------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Category = "MF|Inventory")
	int32 MaxCarry = 6;

	/** Weapon given on spawn and never lost (MF: knife, InventoryGroup 1). */
	UPROPERTY(EditDefaultsOnly, Category = "MF|Inventory")
	TArray<TSubclassOf<AMFWeapon>> DefaultInventory;

	const TArray<TObjectPtr<AMFWeapon>>& GetInventory() const { return Inventory; }
	AMFWeapon* GetCurrentWeapon() const { return CurrentWeapon; }
	int32 GetCurrentCarry() const;
	int32 CountOfClass(TSubclassOf<AMFWeapon> WeaponClass) const;
	AMFWeapon* FindWeaponOfClass(TSubclassOf<AMFWeapon> WeaponClass) const;

	/** Pawn.CanIPickup: carry blocks + MaxCanCarry of this class. */
	bool CanCarry(TSubclassOf<AMFWeapon> WeaponClass) const;

	/** Server: spawn and add a weapon. Returns null if it doesn't fit. */
	AMFWeapon* GiveWeapon(TSubclassOf<AMFWeapon> WeaponClass, bool bEquip = false);
	/** Server: remove (and destroy) a weapon. */
	void RemoveWeapon(AMFWeapon* Weapon, bool bDestroy = true);
	/** Server: drop everything except default weapons. */
	void ClearInventory(bool bKeepDefault);
	/** Server: refill ammo of every carried weapon (MF: entering loadout replenishes ammo). */
	void RefillAmmo();

	/** Switch to a weapon (local input path; forwards to the server). */
	void EquipWeapon(AMFWeapon* Weapon);
	/** Server: kill this soldier (damage, suicide, cheats, falling out of the world). */
	virtual void Die(AController* Killer, TSubclassOf<UDamageType> DamageType, AActor* DamageCauser);
	/** Owning client: you were hit; FromDir = towards the attacker (zero: no direction, e.g. a fall). */
	UFUNCTION(Client, Unreliable)
	void ClientHitFlash(FVector_NetQuantizeNormal FromDir);

	/** Weapon being switched to (current one still putting down), else null. */
	AMFWeapon* GetPendingWeapon() const { return PendingWeapon; }
	void SelectInventoryGroup(int32 Group);
	void CycleWeapon(int32 Direction);
	/** Pick the best weapon to hold (after the current one ran dry / was dropped). */
	void SwitchToBestWeapon();

	/** Called by weapons when they finished being put away. */
	void OnWeaponPutDown(AMFWeapon* Weapon);

	void UpdateCarrySpeed();

	UPROPERTY(BlueprintAssignable, Category = "MF")
	FMFOnInventoryChanged OnInventoryChanged;

	// --- input-driven actions --------------------------------------------------------
	void StartFire(bool bAlt);
	void StopFire(bool bAlt);
	void Reload();
	void SetWalking(bool bWalk);

	bool IsFiring(bool bAlt) const { return bAlt ? bWantsAltFire : bWantsFire; }

	// --- loadout ----------------------------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "MF")
	bool IsInLoadout() const { return bInLoadout; }
	/** Server: enter/leave the loadout screen state. */
	void SetInLoadout(bool bNewInLoadout);
	/**
	 * Server: this loadout is the one a soldier spawns into (MF StartupInLoadout): no damage
	 * while in it and for 5 s after leaving (RagePlayer ReducedDamageType 'All' + AltTimer).
	 */
	void BeginStartupLoadout() { bStartupLoadout = true; }
	bool IsSpawnProtected() const;

	// --- team -------------------------------------------------------------------------
	/** Team id from the player state (MF::NoTeam in deathmatch). */
	UFUNCTION(BlueprintPure, Category = "MF")
	uint8 GetTeam() const;

	// IGenericTeamAgentInterface (AI perception, team checks)
	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(GetTeam()); }

	/** Field of view the camera should use (default FOV, divided by weapon zoom). */
	float GetDesiredFOV() const;

	/** Local view kick (MF PlayerPawn.AddRecoil / ViewRecoil). */
	void AddRecoil(float Amount) { CurrentRecoil += Amount; }

	virtual void Tick(float DeltaSeconds) override;

	/** UE1 PlayerPawn.DefaultFOV */
	UPROPERTY(EditDefaultsOnly, Config, Category = "MF")
	float DefaultFOV = 100.f;

protected:
	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<USceneComponent> WeaponAttach;

	/** Simple body shown to other players until the MF soldier meshes are converted. */
	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	UPROPERTY(ReplicatedUsing = OnRep_Health, BlueprintReadOnly, Category = "MF")
	int32 Health = 100;

	UPROPERTY(Replicated)
	bool bDying = false;

	UPROPERTY(ReplicatedUsing = OnRep_Inventory)
	TArray<TObjectPtr<AMFWeapon>> Inventory;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon)
	TObjectPtr<AMFWeapon> CurrentWeapon;

	/** Weapon to raise once the current one is put away. */
	UPROPERTY()
	TObjectPtr<AMFWeapon> PendingWeapon;

	UPROPERTY(ReplicatedUsing = OnRep_InLoadout)
	bool bInLoadout = false;
	bool bStartupLoadout = false;
	float SpawnProtectedUntil = 0.f;

	bool bWantsFire = false;
	bool bWantsAltFire = false;

	UFUNCTION()
	void OnRep_Health(int32 OldHealth);
	UFUNCTION()
	void OnRep_Inventory();
	UFUNCTION()
	void OnRep_CurrentWeapon(AMFWeapon* OldWeapon);
	UFUNCTION()
	void OnRep_InLoadout();

	UFUNCTION(Server, Reliable)
	void ServerEquipWeapon(AMFWeapon* Weapon);

	void SetCurrentWeapon(AMFWeapon* NewWeapon, AMFWeapon* OldWeapon);
	void ApplyLoadoutVisibility();


	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnDeath(FVector_NetQuantizeNormal Direction);

	/** Absorb damage with carried armour; returns remaining damage. */
	int32 AbsorbWithArmour(int32 Damage, const class UMFDamageType* DamageType);

	float CurrentRecoil = 0.f;
	float RecoilAccumulator = 0.f;
};
