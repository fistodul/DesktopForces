#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Weapons/DFWeapon.h"
#include "DFVehicleTurret.generated.h"

class ADFCharacter;
class ADFProjectile;
class ADFVehicle;
class USoundBase;
class UStaticMeshComponent;

/**
 * A gun on a vehicle worked by whoever takes its seat (MF-mods ArmedAPC hatch gun, jeep's rear
 * MG): it turns towards the gunner's view (pitch limits, turn rate), Fire shoots (instant hit
 * or projectiles), belts run out and reload. The gunner holds an ADFVehicleTurretWeapon
 * meanwhile (nothing in the hands; Fire / R go to the gun). Add one to any ADFVehicle and set
 * GunnerSeat: the vehicle hands its seat changes over.
 */
UCLASS(ClassGroup = MF, meta = (BlueprintSpawnableComponent))
class DESKTOPFORCES_API UDFVehicleTurret : public UActorComponent
{
	GENERATED_BODY()

public:
	UDFVehicleTurret();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: someone took / left a seat of the vehicle. */
	void SeatEntered(ADFCharacter* C, int32 Seat);
	void SeatLeft(ADFCharacter* C, int32 Seat);

	/** Muzzle position and aim in the world. */
	void GetMuzzle(FVector& OutStart, FVector& OutDir) const;
	int32 GetClipAmmo() const { return ClipAmmo; }
	float GetMaxRange() const { return MaxRange; }
	ADFVehicle* GetVehicle() const;
	void Reload();
	/** The gunner's fire button (their turret "weapon" holds it). */
	bool bGunnerFiring = false;

	// tuning (vehicle constructors; MF values in comments)
	UPROPERTY(EditDefaultsOnly, Category = "Turret") int32 GunnerSeat = 1;
	/** Where it turns (vehicle body space, cm), the gun's offset from there and the muzzle's. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret") FVector TurretPivot = FVector::ZeroVector;
	UPROPERTY(EditDefaultsOnly, Category = "Turret") FVector GunOffset = FVector::ZeroVector;
	UPROPERTY(EditDefaultsOnly, Category = "Turret") FVector MuzzleOffset = FVector(90.f, 0.f, 0.f);
	UPROPERTY(EditDefaultsOnly, Category = "Turret") float PitchMin = -11.f;
	UPROPERTY(EditDefaultsOnly, Category = "Turret") float PitchMax = 49.4f;
	/** Degrees per second. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret") float TurnRate = 164.8f;
	/** Where it faces at rest and how far it turns from there (aSeatsRot / aSeatsRotLimit .Yaw; 180 = all round). */
	UPROPERTY(EditDefaultsOnly, Category = "Turret") float BaseYaw = 0.f;
	UPROPERTY(EditDefaultsOnly, Category = "Turret") float YawLimit = 180.f;
	UPROPERTY(EditDefaultsOnly, Category = "Turret") FString TurretMeshName = TEXT("RageWeapons/HeavyMachineGunPickupCarryMesh");
	UPROPERTY(EditDefaultsOnly, Category = "Turret") float TurretDrawScale = 1.f;
	UPROPERTY(EditDefaultsOnly, Category = "Turret") float FireInterval = 0.1f;
	UPROPERTY(EditDefaultsOnly, Category = "Turret") int32 MaxClipAmmo = 100;
	UPROPERTY(EditDefaultsOnly, Category = "Turret") float ReloadTime = 4.f;
	/** Spread: MF Accuracy x 1000 / MaxTraceDistance. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret") float Spread = 0.03f;
	/** None: instant hit traces. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret") TSubclassOf<ADFProjectile> ProjectileClass;
	UPROPERTY(EditDefaultsOnly, Category = "Turret") float Damage = 25.f;
	UPROPERTY(EditDefaultsOnly, Category = "Turret") float MaxRange = 13500.f;
	UPROPERTY(EditDefaultsOnly, Category = "Turret") FString ClassicFireSound = TEXT("WeaponSFX_HeavyMachineGun.Fire");
	UPROPERTY(EditDefaultsOnly, Category = "Turret") FString ClassicReloadSound = TEXT("WeaponSFX_MachineGun.Reload");
	/** The gunner's HUD icon (box on the weapon icon sheet). */
	UPROPERTY(EditDefaultsOnly, Category = "Turret") FIntRect HUDIconRect = FIntRect(64, 64, 256, 128);

protected:
	void Fire(ADFCharacter* Gunner);
	FVector GunPos() const;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> TurretMesh;
	/** Turret rotation relative to the vehicle (server aims it, clients follow). */
	UPROPERTY(Replicated)
	FRotator TurretRot = FRotator::ZeroRotator;
	UPROPERTY(Replicated)
	int32 ClipAmmo = 100;
	float FireDelay = 0.f;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> FireSound;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ReloadSound;
	TWeakObjectPtr<class ADFVehicleTurretWeapon> GunnerWeapon;
	TWeakObjectPtr<ADFWeapon> GunnerLastWeapon;
};

/** What a turret's gunner holds (ArmedAPCWeapon, jeep vehWeap): Fire runs the turret. */
UCLASS(NotPlaceable)
class DESKTOPFORCES_API ADFVehicleTurretWeapon : public ADFWeapon
{
	GENERATED_BODY()

public:
	ADFVehicleTurretWeapon();
	virtual void StartFire(bool bAlt) override;
	virtual void StopFire(bool bAlt) override;
	virtual void StartReload() override;
	virtual bool DrawHUD(UCanvas* Canvas) override;
	TWeakObjectPtr<UDFVehicleTurret> Turret;
};
