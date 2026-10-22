#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GenericTeamAgentInterface.h"
#include "Core/DFTypes.h"
#include "DFCharacter.generated.h"

class ADFWeapon;
class UCameraComponent;
class UDFCharacterMovement;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDFOnInventoryChanged, ADFCharacter*, Character);

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
/** Multipliers on a soldier's body (class defaults x these). */
USTRUCT(BlueprintType)
struct FDFBodyScale
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DF") float Speed = 1.f;        // MaxWalkSpeed
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DF") float Jump = 1.f;         // JumpZVelocity
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DF") float Accel = 1.f;        // MaxAcceleration
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DF") float Step = 1.f;         // MaxStepHeight
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DF") float FallDamage = 1.f;   // MF FallDamageThreshold
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DF") float FallDeath = 1.f;    // MF FallDeathThreshold
};

UCLASS(Config = Game)
class DESKTOPFORCES_API ADFCharacter : public ACharacter, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ADFCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Destroyed() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void PawnClientRestart() override;
	/** The current weapon put away (driving): bring it up again. */
	void ResumeWeapon();
	virtual void Landed(const FHitResult& Hit) override;
	virtual bool CanJumpInternal_Implementation() const override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void FellOutOfWorld(const UDamageType& DmgType) override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	/** The local player looks from behind (ADFPlayerController::BehindView). */
	bool IsBehindView() const;

	UDFCharacterMovement* GetMFMovement() const;
	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }
	USceneComponent* GetWeaponAttachPoint() const { return WeaponAttach; }

	// --- health ---------------------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Replicated, Category = "DF")
	int32 MaxHealth = 100;

	UFUNCTION(BlueprintPure, Category = "DF")
	int32 GetHealth() const { return Health; }
	/** Server: health and its cap (a mapper's placed bot with Health 300 ...). */
	void SetHealthAndMax(int32 NewHealth) { MaxHealth = FMath::Max(1, NewHealth); Health = MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "DF")
	bool IsAlive() const { return Health > 0 && !bDying; }

	/** Deployed HMG and the like (ADFWeapon::IsDeployed): rooted to the spot. */
	bool IsDeployed() const;

	/** The next landing does no harm (dropped in from the air by the mode). */
	bool bNoFallDamageOnce = false;

	/** Total armour charge of carried armour items (HUD). */
	UFUNCTION(BlueprintPure, Category = "DF")
	int32 GetArmourCharge() const;

	/** Restore health (AdrenalineShot). Server only. */
	void Heal(int32 Amount);

	/** MF's headshot rule: hit point above 80% of the collision half-height (Pistol/Rifle.ProcessTraceHit). */
	bool IsHeadshot(const FVector& HitLocation) const;

	// --- inventory ------------------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Replicated, Category = "DF|Inventory")
	int32 MaxCarry = 6;

	/** Server: this soldier's clips never run dry (ADFWeapon::HasInfiniteAmmo: weapons with more
	 *  than one use). Zombie survivors; the InfiniteAmmo cheat. Turning it on fills them up. */
	void SetInfiniteAmmo(bool bOn);
	bool HasInfiniteAmmo() const { return bInfiniteAmmo; }

	/** Server: scale this soldier's body (modes / mods: zombies, survivors); replicated so the
	 *  owner predicts the same movement. Multiplies the class defaults, never stacks. */
	void SetBodyScale(const FDFBodyScale& NewScale);

	/** Weapon given on spawn and never lost (MF: knife, InventoryGroup 1). */
	UPROPERTY(EditDefaultsOnly, Category = "DF|Inventory")
	TArray<TSubclassOf<ADFWeapon>> DefaultInventory;

	const TArray<TObjectPtr<ADFWeapon>>& GetInventory() const { return Inventory; }
	ADFWeapon* GetCurrentWeapon() const { return CurrentWeapon; }

	/** The soldier's animation (null until the MF model is on, and on dedicated servers). */
	class UDFSoldierAnimInstance* GetSoldierAnim() const;
	int32 GetCurrentCarry() const;
	int32 CountOfClass(TSubclassOf<ADFWeapon> WeaponClass) const;
	ADFWeapon* FindWeaponOfClass(TSubclassOf<ADFWeapon> WeaponClass) const;

	/** Server: put a carried weapon on the ground (MF Pawn.DropItem); it keeps its ammo. */
	void DropWeapon(ADFWeapon* Weapon);
	/** Server: take a weapon lying on the ground (MF loadout from dropped weapons). */
	bool PickupWeapon(ADFWeapon* Weapon);

	/** Pawn.CanIPickup: carry blocks + MaxCanCarry of this class. */
	bool CanCarry(TSubclassOf<ADFWeapon> WeaponClass) const;

	/** Server: spawn and add a weapon. Returns null if it doesn't fit. */
	ADFWeapon* GiveWeapon(TSubclassOf<ADFWeapon> WeaponClass, bool bEquip = false);
	/** Server: remove (and destroy) a weapon. */
	void RemoveWeapon(ADFWeapon* Weapon, bool bDestroy = true);
	/** Server: drop everything except default weapons. */
	void ClearInventory(bool bKeepDefault);
	/** Server: refill ammo of every carried weapon (MF: entering loadout replenishes ammo). */
	void RefillAmmo();

	/** Switch to a weapon (local input path; forwards to the server). */
	void EquipWeapon(ADFWeapon* Weapon);
	/** Server: kill this soldier (damage, suicide, cheats, falling out of the world). */
	virtual void Die(AController* Killer, TSubclassOf<UDamageType> DamageType, AActor* DamageCauser);
	/** Owning client: you were hit; FromDir = towards the attacker (zero: no direction, e.g. a fall). */
	UFUNCTION(Client, Unreliable)
	void ClientHitFlash(FVector_NetQuantizeNormal FromDir);

	/** Weapon being switched to (current one still putting down), else null. */
	ADFWeapon* GetPendingWeapon() const { return PendingWeapon; }
	void SelectInventoryGroup(int32 Group);
	void CycleWeapon(int32 Direction);
	/** Pick the best weapon to hold (after the current one ran dry / was dropped). */
	void SwitchToBestWeapon();

	/** Called by weapons when they finished being put away. */
	void OnWeaponPutDown(ADFWeapon* Weapon);

	void UpdateCarrySpeed();

	UPROPERTY(BlueprintAssignable, Category = "DF")
	FDFOnInventoryChanged OnInventoryChanged;

	// --- input-driven actions --------------------------------------------------------
	void StartFire(bool bAlt);
	void StopFire(bool bAlt);
	void Reload();
	void SetWalking(bool bWalk);

	bool IsFiring(bool bAlt) const { return bAlt ? bWantsAltFire : bWantsFire; }

	// --- loadout ----------------------------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "DF")
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
	/** Team id from the player state (DF::NoTeam in deathmatch). */
	UFUNCTION(BlueprintPure, Category = "DF")
	uint8 GetTeam() const;

	// IGenericTeamAgentInterface (AI perception, team checks)
	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(GetTeam()); }

	/** Field of view the camera should use (default FOV, divided by weapon zoom). */
	float GetDesiredFOV() const;

	/** Local view kick (MF PlayerPawn.AddRecoil / ViewRecoil). */
	void AddRecoil(float Amount) { CurrentRecoil += Amount; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnJumped_Implementation() override;

	/** RPlayer sounds (RagePlayerSounds, MiscSFX), heard by everyone around. */
	enum class EBodySound : uint8 { Pain, Death, Jump, Land, LandGrunt, Gasp };
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBodySound(uint8 Kind, float Volume, uint8 Variant);
	/** Says a RagePlayerVoice line (orders, acknowledgements), heard nearby (RageVoicePack:
	 *  Speaker.PlaySound, "only hear messages from nearby players"). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastVoice(FName Line);

	/** Owning client: field of view from the player's settings. */
	void ApplyUserSettings();

	/** UE1 PlayerPawn.DefaultFOV */
	UPROPERTY(EditDefaultsOnly, Config, Category = "DF")
	float DefaultFOV = 100.f;

	/** The soldier model's height against the capsule centre (MF draws it at the collision
	 *  cylinder's centre; cm). */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float SoldierZOffset = 0.f;

	/** PlayerPawn.Bob (MF 0.016; 0 = a still view). */
	UPROPERTY(EditDefaultsOnly, Config, Category = "DF")
	float ViewBob = 0.016f;

protected:
	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<USceneComponent> WeaponAttach;

	/** RPlayer.CheckBob footsteps: stride phase; time of the last pain sound (0.3 s apart). */
	float StepBobTime = 0.f;
	float LastPainSound = -10.f;
	void UpdateFootsteps(float DeltaSeconds);
	/** RPlayer.CheckBob + PlayerCalcView (CameraLocation += WalkBob): the view sways and
	 *  bounces with the stride while walking on the ground. */
	void UpdateViewBob(float DeltaSeconds);
	float ViewBobTime = 0.f;
	FVector ViewBobOffset = FVector::ZeroVector;

	/** Seconds the head may stay under water (RPlayer.UnderWaterTime 20), then 5 a 2 s (PainTimer). */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float UnderWaterTime = 20.f;
	float HeadUnderFor = 0.f;
	FTimerHandle BreathTimer;
	void BreathTick();

	/** MF's soldier (RageGfx RagePlayer1Mesh / 2Mesh) on GetMesh(), wearing the team skins
	 *  (Pawn.SetMultiSkin); checked twice a second on machines that draw it. */
	void UpdateSoldier();
	FTimerHandle SoldierTimer;
	int32 SoldierModel = -1;
	int32 SoldierSkin = -1;

	/** Simple body shown to other players until the MF soldier meshes are converted. */
	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	/** RagePlayer.RageTick (Gregaras): Jump held, falling faster than 150 uu/s: the canopy opens,
	 *  the fall slows to 200 uu/s and the soldier glides the way they look. */
	void UpdateParachute(float DeltaSeconds);
	UPROPERTY(ReplicatedUsing = OnRep_Parachute)
	bool bParachute = false;
	UFUNCTION()
	void OnRep_Parachute();
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ParachuteMesh;

	UPROPERTY(ReplicatedUsing = OnRep_Health, BlueprintReadOnly, Category = "DF")
	int32 Health = 100;

	UPROPERTY(ReplicatedUsing = OnRep_BodyScale)
	FDFBodyScale BodyScale;
	UFUNCTION()
	void OnRep_BodyScale() { ApplyBodyScale(); }
	void ApplyBodyScale();

	UPROPERTY(Replicated)
	bool bDying = false;

	UPROPERTY(ReplicatedUsing = OnRep_Inventory)
	TArray<TObjectPtr<ADFWeapon>> Inventory;
	UPROPERTY(Replicated)
	bool bInfiniteAmmo = false;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon)
	TObjectPtr<ADFWeapon> CurrentWeapon;

	/** Weapon to raise once the current one is put away. */
	UPROPERTY()
	TObjectPtr<ADFWeapon> PendingWeapon;

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
	void OnRep_CurrentWeapon(ADFWeapon* OldWeapon);
	UFUNCTION()
	void OnRep_InLoadout();

	UFUNCTION(Server, Reliable)
	void ServerEquipWeapon(ADFWeapon* Weapon);

	void SetCurrentWeapon(ADFWeapon* NewWeapon, ADFWeapon* OldWeapon);
	void ApplyLoadoutVisibility();


	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnDeath(FVector_NetQuantizeNormal Direction);

	/** Absorb damage with carried armour; returns remaining damage. */
	int32 AbsorbWithArmour(int32 Damage, const class UDFDamageType* DamageType);

	float CurrentRecoil = 0.f;
	float RecoilAccumulator = 0.f;
};
