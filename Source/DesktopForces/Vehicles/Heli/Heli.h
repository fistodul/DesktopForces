#pragma once

#include "CoreMinimal.h"
#include "Vehicles/DFVehicle.h"
#include "Heli.generated.h"

/**
 * MF-mods heli (model Marcin, edited by Gregaras; code by Gregaras from MF's vehicles; MPL-2.0
 * with his agreement). The community servers put it at every Truck generator (mutPack
 * replaceVehicles).
 *
 * Flight (heli state Driving.Tick, MF velocities in m/s, per 60 Hz tick): getting in spins the
 * rotors up (engine pitch 15 -> 58 at 0.15 a tick, ~5 s), then it lifts 50 uu and holds a hover
 * height: below it, it climbs by (height difference / 500 uu, at most 2) m/s a tick; Jump raises
 * the height 100 uu, Crouch lowers it 55 uu. Forward / back / strafe add 0.1 m/s a tick along
 * its heading and tilt it (nose down to between 0.3 and 0.6 of the way, sideways the same);
 * with no keys it slows 1 % a tick and levels out. Fire / alt fire turn it left / right. Get
 * out in the air and it drops. Four cockpit panes (300 health each) shatter and come back
 * after 20 s. Bots only ride along when a player flies it.
 */
UCLASS()
class DESKTOPFORCES_API AHeli : public ADFVehicle {
	GENERATED_BODY()

public:
	AHeli();
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual float BotDesirability(const ADFCharacter* Bot) const override;

protected:
	virtual void BeginPlay() override;
	virtual void SimulateDrive(float Dt) override;
	virtual float GetEnginePitch() const override { return EnginePitch / 64.f; }

	UPROPERTY(VisibleAnywhere, Category = "Heli")
	TObjectPtr<UStaticMeshComponent> MainRotor;
	UPROPERTY(VisibleAnywhere, Category = "Heli")
	TObjectPtr<UStaticMeshComponent> TailRotor;
	UPROPERTY(VisibleAnywhere, Category = "Heli")
	TArray<TObjectPtr<UStaticMeshComponent>> Glass;

	/** SoundPitch: 15 at rest, idlePitch 58 with the engine running; the rotors spin with it. */
	float EnginePitch = 15.f;
	float MainRotorYaw = 0.f;
	float TailRotorPitch = 0.f;
	/** hoverHeight (cm, world Z of the body). */
	float HoverHeight = 0.f;

	/** heliGlass: Health 300, timeUntilRegeneration 20. */
	TArray<int32> GlassHealth;
	TArray<FTimerHandle> GlassTimers;
	UFUNCTION(NetMulticast, Reliable)
	void MulticastGlass(uint8 Pane, uint8 State); // 0 hit, 1 cracked, 2 shattered, 3 back
	void SetGlassIntact(int32 Pane, bool bIntact);
};

/**
 * heliHmg: the heli with a jeep gun in each side door (vehWeap at vehicleWeapPos, facing out:
 * vehicleWeapRot +-90, a 90 degree arc each way); its gunners stand at seats 2 and 3.
 */
UCLASS()
class DESKTOPFORCES_API AHeliHmg : public AHeli {
	GENERATED_BODY()

public:
	AHeliHmg();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Heli")
	TObjectPtr<class UDFVehicleTurret> RightGun;
	UPROPERTY(VisibleAnywhere, Category = "Heli")
	TObjectPtr<class UDFVehicleTurret> LeftGun;
};
