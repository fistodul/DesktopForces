#pragma once

#include "CoreMinimal.h"
#include "Vehicles/DFVehicle.h"
#include "Humvee.generated.h"

/**
 * MF's own cars (RageGame). Numbers come from the game's own data: Physics/handling.hnd2 (mass, top
 * speed in mph, steering lock), Physics/<Vehicle>.COL (wheel spheres, metres: x right, y up,
 * z forward) and the Rage classes (seats, doors, camera distance, armour; uu x 2.25 = cm).
 */
UCLASS()
class DESKTOPFORCES_API AHumvee : public ADFVehicle
{
	GENERATED_BODY()
public:
	AHumvee();
};
