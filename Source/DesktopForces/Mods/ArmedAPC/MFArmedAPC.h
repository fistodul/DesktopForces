#pragma once

#include "CoreMinimal.h"
#include "Vehicles/MFVehicles.h"
#include "Weapons/MFWeapon.h"
#include "MFArmedAPC.generated.h"

class AMFProjectile;
class USoundBase;

/**
 * Armed APC (MF-mods ArmedAPC): the Armadillo with a heavy machine gun on its roof hatch. The
 * passenger in the gunner seat aims it with their view and fires it with Fire (100 a belt,
 * 10 a second, 25 damage, 4 s to reload; R reloads early). ?ArmedAPC=1 puts it at the
 * map's Armadillo generators, ?ArmedAPC=2 the rocket version.
 */
UCLASS()
class DESKTOPFORCES_API AMFVehicle_ArmedAPC : public AMFVehicle_Armadillo
{
	GENERATED_BODY()

public:
	AMFVehicle_ArmedAPC();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Muzzle position and aim in the world. */
	void GetMuzzle(FVector& OutStart, FVector& OutDir) const;
	int32 GetClipAmmo() const { return ClipAmmo; }
	float GetMaxRange() const { return MaxRange; }
	void ReloadTurret();

	/** The gunner's fire button (their turret "weapon" holds it). */
	bool bGunnerFiring = false;

protected:
	virtual void BeginPlay() override;
	virtual void PassengerEntered(AMFCharacter* C, int32 Seat) override;
	virtual void PassengerLeft(AMFCharacter* C, int32 Seat) override;
	void FireTurret(AMFCharacter* Gunner);
	FVector GunPos() const;

	UPROPERTY(VisibleAnywhere, Category = "APC")
	TObjectPtr<UStaticMeshComponent> TurretMesh;

	/** Turret rotation relative to the vehicle (server aims it, clients follow). */
	UPROPERTY(Replicated)
	FRotator TurretRot = FRotator::ZeroRotator;
	UPROPERTY(Replicated)
	int32 ClipAmmo = 100;

	// tuning (ArmedAPC defaultproperties; uu -> cm, UE1 rotation units -> degrees)
	UPROPERTY(EditDefaultsOnly, Category = "APC") int32 GunnerSeat = 1;
	UPROPERTY(EditDefaultsOnly, Category = "APC") FVector TurretPivot = FVector(0.f, 0.f, 189.f);   // Z 84
	UPROPERTY(EditDefaultsOnly, Category = "APC") FVector GunOffset = FVector(67.5f, 0.f, 0.f);     // X 30
	UPROPERTY(EditDefaultsOnly, Category = "APC") FVector MuzzleOffset = FVector(90.f, 0.f, 0.f);   // X 40
	UPROPERTY(EditDefaultsOnly, Category = "APC") float PitchMin = -11.f;          // -2000
	UPROPERTY(EditDefaultsOnly, Category = "APC") float PitchMax = 49.4f;          // 9000
	UPROPERTY(EditDefaultsOnly, Category = "APC") float TurnRate = 164.8f;         // 30000 / s
	UPROPERTY(EditDefaultsOnly, Category = "APC") FString TurretMeshName = TEXT("RageWeapons/HeavyMachineGunPickupCarryMesh");
	UPROPERTY(EditDefaultsOnly, Category = "APC") float TurretDrawScale = 1.25f;
	UPROPERTY(EditDefaultsOnly, Category = "APC") float FireInterval = 0.1f;
	UPROPERTY(EditDefaultsOnly, Category = "APC") int32 MaxClipAmmo = 100;
	UPROPERTY(EditDefaultsOnly, Category = "APC") float ReloadTime = 4.f;
	UPROPERTY(EditDefaultsOnly, Category = "APC") float Spread = 0.03f;
	/** None: instant hit traces. */
	UPROPERTY(EditDefaultsOnly, Category = "APC") TSubclassOf<AMFProjectile> ProjectileClass;
	UPROPERTY(EditDefaultsOnly, Category = "APC") float Damage = 25.f;
	UPROPERTY(EditDefaultsOnly, Category = "APC") float Momentum = 2250.f;          // 1000
	UPROPERTY(EditDefaultsOnly, Category = "APC") float MaxRange = 13500.f;         // 6000 uu
	UPROPERTY(EditDefaultsOnly, Category = "APC") FString MFFireSound = TEXT("WeaponSFX_HeavyMachineGun.Fire");
	UPROPERTY(EditDefaultsOnly, Category = "APC") FString MFReloadSound = TEXT("WeaponSFX_MachineGun.Reload");

	float FireDelay = 0.f;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> FireSound;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ReloadSoundAsset;
	TWeakObjectPtr<class AMFWeapon_APCTurret> GunnerWeapon;
	TWeakObjectPtr<AMFWeapon> GunnerLastWeapon;
};

/** Armed APC with a rocket launcher on the hatch (ArmedAPCRocket): 4 rockets, 0.8 s apart. */
UCLASS()
class DESKTOPFORCES_API AMFVehicle_ArmedAPCRocket : public AMFVehicle_ArmedAPC
{
	GENERATED_BODY()
public:
	AMFVehicle_ArmedAPCRocket();
};

/** What the gunner holds while in the turret seat (ArmedAPCWeapon): Fire runs the turret. */
UCLASS(NotPlaceable)
class DESKTOPFORCES_API AMFWeapon_APCTurret : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_APCTurret();
	virtual void StartFire(bool bAlt) override;
	virtual void StopFire(bool bAlt) override;
	virtual void StartReload() override;
	virtual bool DrawHUD(UCanvas* Canvas) override;

	TWeakObjectPtr<AMFVehicle_ArmedAPC> APC;
};
