#pragma once

#include "CoreMinimal.h"
#include "Vehicles/DFVehicle.h"
#include "Jeep.generated.h"

class UDFVehicleTurret;

/**
 * MF-mods jeep (model Marcin, code Gregaras): a light 4x4 (handling JEEP: 1000 kg, 60 mph,
 * lock 60) with two seats up front and a machine gun on the back (hmgSeat 2: vehWeap, a
 * MachineGun with a 3000 round belt, turned with the gunner's view). The community servers put
 * one by every truck generator (mutPack addVehicles); here: ?Trucks=Jeep, or a map's
 * VehicleGenerator with VehicleClass=jeep.jeep.
 */
UCLASS()
class DESKTOPFORCES_API AJeep : public ADFVehicle
{
	GENERATED_BODY()

public:
	AJeep();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Jeep")
	TObjectPtr<UDFVehicleTurret> Gun;
};
