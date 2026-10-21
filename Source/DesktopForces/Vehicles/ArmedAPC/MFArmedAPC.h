#pragma once

#include "CoreMinimal.h"
#include "Vehicles/Armadillo/MFVehicle_Armadillo.h"
#include "MFArmedAPC.generated.h"

class UMFVehicleTurret;

/**
 * Armed APC (MF-mods ArmedAPC): the Armadillo with a heavy machine gun on its roof hatch. The
 * passenger in the gunner seat aims it with their view and fires it with Fire (100 a belt,
 * 10 a second, 25 damage, 4 s to reload; R reloads early). ?ArmedAPC=1 puts it at the
 * map's Armadillo generators, ?ArmedAPC=2 the rocket version. The gun: UMFVehicleTurret.
 */
UCLASS()
class DESKTOPFORCES_API AMFVehicle_ArmedAPC : public AMFVehicle_Armadillo
{
	GENERATED_BODY()

public:
	AMFVehicle_ArmedAPC();

protected:
	UPROPERTY(VisibleAnywhere, Category = "APC")
	TObjectPtr<UMFVehicleTurret> Turret;
};

/** Armed APC with a rocket launcher on the hatch (ArmedAPCRocket): 4 rockets, 0.8 s apart. */
UCLASS()
class DESKTOPFORCES_API AMFVehicle_ArmedAPCRocket : public AMFVehicle_ArmedAPC
{
	GENERATED_BODY()

public:
	AMFVehicle_ArmedAPCRocket();
};
