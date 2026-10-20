#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/MFTypes.h"
#include "MFWeapon.generated.h"

class AMFCharacter;
class AMFProjectile;
class UCanvas;
class USoundBase;
class UStaticMeshComponent;
class UTexture2D;

UENUM(BlueprintType)
enum class EMFWeaponState : uint8
{
	Inactive,     // holstered (in inventory, not held)
	Equipping,    // SelectUp animation
	Idle,
	Firing,       // a shot's animation is playing; MF Finish() runs when it ends
	Reloading,
	PuttingDown,  // SelectDown animation
	Zooming,      // AccurateAimUp / BringScopeUp
};

/**
 * Base class of every inventory item (MF: Weapon > RageWeapon). Armour and the
 * adrenaline shot are weapons too, exactly like in Mobile Forces.
 *
 * Firing model (RageWeapon): a shot starts an animation; when it ends Finish() decides
 * what's next - reload if the clip is dry, fire again if the button is still held,
 * otherwise idle. So fire rates are the original animation lengths (FireTime etc.,
 * taken from RageWeapons.u, see Docs/Gameplay.md).
 *
 * Networking: input goes to the server (ServerStartFire/StopFire); the server runs the
 * state machine, traces and applies damage. The owning client runs the same state
 * machine for instant feedback (sounds, recoil, ammo prediction); other clients play
 * effects when BurstCounter changes. Ammo replicates to the owner only.
 *
 * Performance: weapons never tick; all timing uses timers.
 */
UCLASS(Abstract, Config = Game)
class MOBILEFORCES_API AMFWeapon : public AActor
{
	GENERATED_BODY()

public:
	AMFWeapon();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	virtual void Destroyed() override;

	// --- identity / loadout ---------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	FText DisplayName;

	/** Number key (MF InventoryGroup: 1 knife, 2 pistol ... 10 adrenaline, 11 armour). */
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	int32 InventoryGroup = 1;

	/**
	 * Key that selects this item: 1-9, 0 for groups 1-10, X for every group above
	 * (armour, mod gadgets). MF had 11 groups but no key for the 11th.
	 */
	int32 GetSlotKey() const { return FMath::Clamp(InventoryGroup, 1, 11); }
	static FString SlotKeyName(int32 Key) { return Key >= 11 ? TEXT("X") : FString::FromInt(Key % 10); }

	/** Inventory blocks used (MF CarrySize); a soldier carries 6. */
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	int32 CarrySize = 0;

	/** How many of this item can be carried (MF MaxCanCarry). */
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	int32 MaxCanCarry = 1;

	UPROPERTY(EditDefaultsOnly, Category = "MF")
	bool bLoadoutSelectable = true;

	/** Append to the game-wide loadout list when this class is loaded (mods set this). */
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	bool bAddToLoadout = false;

	/** Can be held in hands (armour can't). */
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	bool bSelectable = true;

	/** Never removed from the inventory (the knife). */
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	bool bUndroppable = false;

	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float AutoSwitchPriority = 5.f;

	/** Bot weapon preference (MF AIRating). */
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float AIRating = 0.5f;

	// --- ammo -----------------------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Category = "MF|Ammo")
	int32 MaxClipAmmo = 1;

	UPROPERTY(EditDefaultsOnly, Category = "MF|Ammo")
	int32 MaxClips = 1;

	/** Remove from inventory once the last round is used (grenades, trip bombs, adrenaline). */
	UPROPERTY(EditDefaultsOnly, Category = "MF|Ammo")
	bool bDestroyWhenEmpty = false;

	/** No ammo label on the HUD (MF armour: GetAmmoStatus ""). */
	UPROPERTY(EditDefaultsOnly, Category = "MF|Ammo")
	bool bHideAmmoCount = false;

	UFUNCTION(BlueprintPure, Category = "MF")
	int32 GetClipAmmo() const { return ClipAmmo; }
	UFUNCTION(BlueprintPure, Category = "MF")
	int32 GetTotalAmmo() const { return TotalAmmo; }
	int32 GetAmmoCount() const { return ClipAmmo + TotalAmmo; }
	bool HasAmmoInClip() const { return ClipAmmo > 0; }
	bool CanReload() const { return TotalAmmo > 0 && ClipAmmo < MaxClipAmmo; }
	bool HasAnyAmmo() const { return GetAmmoCount() > 0; }

	/** RageWeapon.GiveFullAmmo: full clip + (MaxClips - 1) spare clips. */
	virtual void GiveFullAmmo();

	// --- timing (seconds) -------------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Category = "MF|Timing")
	float FireTime = 0.3f;

	/** Firing while zoomed in (MF AccurateAimFire animation). */
	UPROPERTY(EditDefaultsOnly, Category = "MF|Timing")
	float ZoomedFireTime = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "MF|Timing")
	float ReloadTime = 1.3f;

	UPROPERTY(EditDefaultsOnly, Category = "MF|Timing")
	float EquipTime = 0.7f;

	UPROPERTY(EditDefaultsOnly, Category = "MF|Timing")
	float PutDownTime = 0.7f;

	/** Raising / lowering the sights (AccurateAimUp/Down). */
	UPROPERTY(EditDefaultsOnly, Category = "MF|Timing")
	float ZoomTime = 0.35f;

	// --- hitscan ------------------------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Category = "MF|Hit")
	bool bInstantHit = true;

	UPROPERTY(EditDefaultsOnly, Category = "MF|Hit")
	float HitDamage = 20.f;

	/** Damage when IsHeadshot(); 0 = no headshot bonus. */
	UPROPERTY(EditDefaultsOnly, Category = "MF|Hit")
	float HeadshotDamage = 0.f;

	/** Impulse applied to the victim along the shot (MF: HitNormal * 1000). */
	UPROPERTY(EditDefaultsOnly, Category = "MF|Hit")
	float HitMomentum = 2250.f;

	/** Spread: shot end point offset by Accuracy * rand(-0.5..0.5) * 2250 cm sideways and up (MF TraceFire, 1000 uu). */
	UPROPERTY(EditDefaultsOnly, Category = "MF|Hit")
	float Accuracy = 0.5f;

	/** Spread while zoomed (MF AltAccuracy). */
	UPROPERTY(EditDefaultsOnly, Category = "MF|Hit")
	float AltAccuracy = 0.1f;

	/** Trace length (MF RageWeapon.MaxTraceDistance 10000 uu). */
	UPROPERTY(EditDefaultsOnly, Category = "MF|Hit")
	float MaxTraceDistance = 22500.f;

	UPROPERTY(EditDefaultsOnly, Category = "MF|Hit")
	int32 Pellets = 1;

	UPROPERTY(EditDefaultsOnly, Category = "MF|Hit")
	TSubclassOf<UMFDamageType> DamageType;

	UPROPERTY(EditDefaultsOnly, Category = "MF|Hit")
	TSubclassOf<UMFDamageType> HeadshotDamageType;

	// --- projectiles ---------------------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Category = "MF|Projectile")
	TSubclassOf<AMFProjectile> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, Category = "MF|Projectile")
	TSubclassOf<AMFProjectile> AltProjectileClass;

	// --- zoom (alt fire on most guns) -----------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Category = "MF|Zoom")
	int32 MaxZoomLevel = 0;

	/** FOV divisor when zoomed (MF ZoomScale). */
	UPROPERTY(EditDefaultsOnly, Category = "MF|Zoom")
	float ZoomScale = 1.f;

	/** Movement speed multiplier while zoomed / aiming. */
	UPROPERTY(EditDefaultsOnly, Category = "MF|Zoom")
	float ZoomedMoveScale = 0.4f;

	UFUNCTION(BlueprintPure, Category = "MF")
	bool IsZoomed() const { return ZoomLevel > 0; }

	/** FOV multiplier for the owner's camera (1 = not zoomed). */
	virtual float GetFOVScale() const;
	/** Movement multiplier for the owner. */
	virtual float GetMoveSpeedScale() const;

	// --- recoil ---------------------------------------------------------------------------
	/** MF RecoilPower: PlayerPawn.AddRecoil(RecoilPower * 6). */
	UPROPERTY(EditDefaultsOnly, Category = "MF|Recoil")
	float RecoilPower = 1.f;

	// --- presentation ---------------------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Category = "MF|FX")
	TObjectPtr<USoundBase> FireSound;
	UPROPERTY(EditDefaultsOnly, Category = "MF|FX")
	TObjectPtr<USoundBase> ReloadSound;
	UPROPERTY(EditDefaultsOnly, Category = "MF|FX")
	TObjectPtr<USoundBase> SelectSound;
	UPROPERTY(EditDefaultsOnly, Category = "MF|FX")
	TObjectPtr<USoundBase> ZoomInSound;
	UPROPERTY(EditDefaultsOnly, Category = "MF|FX")
	TObjectPtr<USoundBase> ZoomOutSound;

	/** Soft path of the converted MF sound to load for FireSound if not set (e.g. "WeaponSFX_Pistol.Fire"). */
	UPROPERTY(EditDefaultsOnly, Category = "MF|FX")
	FString MFFireSound;
	UPROPERTY(EditDefaultsOnly, Category = "MF|FX")
	FString MFReloadSound;
	UPROPERTY(EditDefaultsOnly, Category = "MF|FX")
	FString MFSelectSound;

	/** HUD / loadout icon: texture + pixel rect inside it (MF RageWeapon.WeaponIcon TexRect). */
	UPROPERTY(EditDefaultsOnly, Category = "MF|FX")
	TSoftObjectPtr<UTexture2D> HUDIcon;

	/** X, Y, W, H in texture pixels. */
	UPROPERTY(EditDefaultsOnly, Category = "MF|FX")
	FIntRect HUDIconRect = FIntRect(0, 0, 64, 64);

	/**
	 * Converted MF carry / pickup model ("RageWeapons/PistolPickupCarryMesh", mfport props):
	 * held in first person (at ViewOffset from the camera) and lying on the floor when
	 * dropped. Without a converted install the placeholder box stays.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MF|FX")
	FString ViewMesh;
	/** Held: rear of the model this far in front of the eye, centre this far right, top this far up (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "MF|FX")
	FVector ViewOffset = FVector(10.f, 9.f, -5.f);
	UPROPERTY(EditDefaultsOnly, Category = "MF|FX")
	FRotator ViewRotation = FRotator::ZeroRotator;
	/** Held model scale (MF PlayerViewScale-like: the carry models are life size). */
	UPROPERTY(EditDefaultsOnly, Category = "MF|FX")
	float ViewScale = 0.5f;
	bool bHasViewMesh = false;
	FTransform HandTransform;

	/** First-person model (placeholder until the MF weapon meshes are converted). */
	UPROPERTY(VisibleAnywhere, Category = "MF|FX")
	TObjectPtr<UStaticMeshComponent> Mesh1P;

	// --- interface used by AMFCharacter / HUD -------------------------------------------
	AMFCharacter* GetOwnerCharacter() const;
	EMFWeaponState GetState() const { return State; }
	/** Fire / alt-fire button currently held on this machine. */
	bool IsFiring(bool bAlt) const { return bAlt ? bAltFireHeld : bFireHeld; }

	virtual void OnGiven(AMFCharacter* NewOwner);
	virtual void OnEquip();
	virtual void OnPutDown();
	/** Immediately holster without animation (death, loadout). */
	virtual void ForceInactive();

	// --- on the ground (MF RageWeapon state Pickup) -------------------------------------
	/** Lying in the world without an owner; players take it through their loadout
	 *  (MF TryLoadoutDropped: no touch pickup). Keeps its ammo. */
	bool IsDropped() const { return bDropped; }
	/** Server: leave the owner and come to rest on the floor near From (MF Pawn.DropItem). */
	virtual void DropFrom(const FVector& From, const FRotator& Facing);
	/** Server: taken from the ground by NewOwner (before AMFCharacter adds it to the inventory). */
	virtual void OnPickedUp(AMFCharacter* NewOwner);
	/** Most weapons lying around per map; the oldest disappears beyond it. */
	static constexpr int32 MaxDropped = 32;

	virtual void StartFire(bool bAlt);
	virtual void StopFire(bool bAlt);
	virtual void StartReload();

	/** Armour hook: return the damage left after this item absorbed its share. */
	virtual int32 AbsorbDamage(int32 Damage, const UMFDamageType* Type) { return Damage; }
	/** Order of the AbsorbDamage calls, highest first (MF Inventory.AbsorptionPriority). */
	virtual int32 GetAbsorbPriority() const { return 0; }

	/** HUD hook: crosshair / scope overlay. Return true if the weapon drew its own crosshair. */
	virtual bool DrawHUD(UCanvas* Canvas) { return false; }

	/** Extra status line for the HUD (e.g. rifle power). */
	virtual FString GetHUDStatus() const;

protected:
	UPROPERTY(Transient, Replicated)
	int32 ClipAmmo = 0;

	UPROPERTY(Transient, Replicated)
	int32 TotalAmmo = 0;

	UPROPERTY(Transient, ReplicatedUsing = OnRep_ZoomLevel)
	uint8 ZoomLevel = 0;

	UPROPERTY(Transient, ReplicatedUsing = OnRep_Dropped)
	bool bDropped = false;
	float DroppedTime = 0.f;
	UFUNCTION()
	void OnRep_Dropped();
	void UpdateDroppedLook();

	/** Incremented per shot; simulated proxies play effects on change. */
	UPROPERTY(Transient, ReplicatedUsing = OnRep_BurstCounter)
	uint8 BurstCounter = 0;

	EMFWeaponState State = EMFWeaponState::Inactive;
	FTimerHandle StateTimer;
	bool bFireHeld = false;
	bool bAltFireHeld = false;
	bool bPendingReload = false;
	bool bLastShotAlt = false;

	UFUNCTION(Server, Reliable)
	void ServerStartFire(bool bAlt);
	UFUNCTION(Server, Reliable)
	void ServerStopFire(bool bAlt);
	UFUNCTION(Server, Reliable)
	void ServerReload();

	UFUNCTION()
	void OnRep_BurstCounter();
	UFUNCTION()
	void OnRep_ZoomLevel(uint8 OldZoom);

	/** Whether this machine runs gameplay state (server, or the owning client predicting). */
	bool IsSimulatingLocally() const;
	bool IsLocallyControlled() const;

	void SetState(EMFWeaponState NewState, float Duration);
	void ClearStateTimer();
	UFUNCTION()
	void OnStateTimer();

	/** RageWeapon.Finish(): pick the next state after a shot / reload / zoom. */
	virtual void Finish();

	/** Primary press; returns true if a shot started. */
	virtual bool TryFire(bool bAlt);
	/** Alt press for weapons whose alt fire is a zoom toggle. */
	virtual void ToggleZoom();

	/** Fire one shot (both on server and on the predicting client). */
	virtual void FireShot(bool bAlt);

	/** Server: hitscan per MF TraceFire. */
	virtual void TraceFire(float InAccuracy, int32 InPellets, bool bAlt);
	/** Server: what happens on a hitscan hit (MF ProcessTraceHit). */
	virtual void ProcessTraceHit(const FHitResult& Hit, const FVector& ShotDir, bool bAlt);
	/** Server: spawn a projectile. */
	virtual AMFProjectile* SpawnProjectile(TSubclassOf<AMFProjectile> Class, float Speed, bool bAlt);

	/** Cosmetic: sound, recoil, muzzle flash. Runs on the shooter and simulated proxies. */
	virtual void PlayFireEffects(bool bAlt);
	/** Called on the server when a shot consumed the last round of a bDestroyWhenEmpty item. */
	virtual void OnDepleted();

	void ReloadAmmo();

	FVector GetFireStart() const;
	FRotator GetAimRotation() const;

	void PlaySoundLocal(USoundBase* Sound) const;
	void ResolveSounds();
};
