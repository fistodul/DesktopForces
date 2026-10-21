#pragma once

#include "CoreMinimal.h"
#include "Vehicles/MFVehicle.h"
#include "Vehicles/Humvee/MFVehicle_Humvee.h"
#include "MFVehicle_Trailer.generated.h"

/** Trailer mode's cab (MF TrailerCab extends Humvee): tows the bomb trailer. */
UCLASS()
class DESKTOPFORCES_API AMFVehicle_TrailerCab : public AMFVehicle_Humvee
{
	GENERATED_BODY()
public:
	AMFVehicle_TrailerCab();
};

/** MF TrailerX / BombTrailer: no engine, no seats, towed by a cab. */
UCLASS()
class DESKTOPFORCES_API AMFVehicle_Trailer : public AMFVehicle
{
	GENERATED_BODY()
public:
	AMFVehicle_Trailer();
};
