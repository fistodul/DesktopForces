#pragma once

#include "CoreMinimal.h"
#include "Vehicles/MFVehicle.h"
#include "MFVehicle_Jeep.generated.h"

class UMFVehicleTurret;

/**
 * MF-mods jeep (model Marcin, code Gregaras): a light 4x4 (handling JEEP: 1000 kg, 60 mph,
 * lock 60) with two seats up front and a machine gun on the back (hmgSeat 2: vehWeap, a
 * MachineGun with a 3000 round belt, turned with the gunner's view). The community servers put
 * one by every truck generator (mutPack addVehicles); here: ?Jeeps=1, or a map's
 * VehicleGenerator with VehicleClass=jeep.jeep.
 */
UCLASS()
class DESKTOPFORCES_API AMFVehicle_Jeep : public AMFVehicle
{
	GENERATED_BODY()

public:
	AMFVehicle_Jeep();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Jeep")
	TObjectPtr<UMFVehicleTurret> Gun;
};
