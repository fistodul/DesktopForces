#pragma once

#include "CoreMinimal.h"
#include "Vehicles/MFVehicle.h"
#include "MFVehicle_Boat.generated.h"

/**
 * MF-mods boat (Gregaras; CC0 model): floats on the map's water plane (PhysicalWaterInfo,
 * handling BOAT Buoyancy 50) and only drives while it does - the keel in the water, the deck
 * out of it. Throttle ramps up (0.004 a tick to 0.85), top speed 0.85 x 12 m/s forward, 0.2 x 12
 * back; steering spins it (0.05 a tick, reversed when backing). Three seats, the Humvee's gun.
 * Maps: VehicleGenerator VehicleClass=boat.boat (BR-thebigisland).
 */
UCLASS()
class DESKTOPFORCES_API AMFVehicle_Boat : public AMFVehicle
{
	GENERATED_BODY()

public:
	AMFVehicle_Boat();

protected:
	virtual void SimulateDrive(float Dt) override;

	UPROPERTY(EditDefaultsOnly, Category = "Boat")
	float MaxSpeedForward = 1020.f;     // maxSpeedForward 0.85 x 12 (m/s -> cm/s)
	UPROPERTY(EditDefaultsOnly, Category = "Boat")
	float MaxSpeedBackward = 240.f;     // maxSpeedBackward 0.2 x 12
	UPROPERTY(EditDefaultsOnly, Category = "Boat")
	float TurnRate = 3.f;               // turnVelocityMultiplier 0.05 rad/s a tick at 60 Hz

	float AccelMultiplier = 0.f;        // accelerationVelocityMultiplier
};
