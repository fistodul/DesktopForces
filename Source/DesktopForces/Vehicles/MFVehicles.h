#pragma once

#include "CoreMinimal.h"
#include "Vehicles/MFVehicle.h"
#include "MFVehicles.generated.h"

/**
 * The MF vehicles. Numbers come from the game's own data: Physics/handling.hnd2 (mass, top
 * speed in mph, steering lock), Physics/<Vehicle>.COL (wheel spheres, metres: x right, y up,
 * z forward) and the Rage classes (seats, doors, camera distance, armour; uu x 2.25 = cm).
 */
UCLASS()
class DESKTOPFORCES_API AMFVehicle_Buggy : public AMFVehicle
{
	GENERATED_BODY()
public:
	AMFVehicle_Buggy();
};

UCLASS()
class DESKTOPFORCES_API AMFVehicle_Humvee : public AMFVehicle
{
	GENERATED_BODY()
public:
	AMFVehicle_Humvee();
};

UCLASS()
class DESKTOPFORCES_API AMFVehicle_Truck : public AMFVehicle
{
	GENERATED_BODY()
public:
	AMFVehicle_Truck();
};

/** The APC. */
UCLASS()
class DESKTOPFORCES_API AMFVehicle_Armadillo : public AMFVehicle
{
	GENERATED_BODY()
public:
	AMFVehicle_Armadillo();
};

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
