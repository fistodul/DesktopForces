#pragma once

#include "CoreMinimal.h"
#include "Vehicles/DFVehicle.h"
#include "Vehicles/Humvee/Humvee.h"
#include "Trailer.generated.h"

/** Trailer mode's cab (MF TrailerCab extends Humvee): tows the bomb trailer. */
UCLASS()
class DESKTOPFORCES_API ATrailerCab : public AHumvee {
	GENERATED_BODY()
public:
	ATrailerCab();
};

/** MF TrailerX / BombTrailer: no engine, no seats, towed by a cab. */
UCLASS()
class DESKTOPFORCES_API ATrailer : public ADFVehicle {
	GENERATED_BODY()
public:
	ATrailer();
};
