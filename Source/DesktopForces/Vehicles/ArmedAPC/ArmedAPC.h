#pragma once

#include "CoreMinimal.h"
#include "Vehicles/Armadillo/Armadillo.h"
#include "ArmedAPC.generated.h"

class UDFVehicleTurret;

/**
 * Armed APC (MF-mods ArmedAPC): the Armadillo with a heavy machine gun on its roof hatch. The
 * passenger in the gunner seat aims it with their view and fires it with Fire (100 a belt,
 * 10 a second, 25 damage, 4 s to reload; R reloads early). ?APCs=ArmedAPC puts it at the
 * map's Armadillo generators, ?APCs=ArmedAPCRocket the rocket version. The gun: UDFVehicleTurret.
 */
UCLASS()
class DESKTOPFORCES_API AArmedAPC : public AArmadillo {
	GENERATED_BODY()

public:
	AArmedAPC();

protected:
	UPROPERTY(VisibleAnywhere, Category = "APC")
	TObjectPtr<UDFVehicleTurret> Turret;
};

/** Armed APC with a rocket launcher on the hatch (ArmedAPCRocket): 4 rockets, 0.8 s apart. */
UCLASS()
class DESKTOPFORCES_API AArmedAPCRocket : public AArmedAPC {
	GENERATED_BODY()

public:
	AArmedAPCRocket();
};
