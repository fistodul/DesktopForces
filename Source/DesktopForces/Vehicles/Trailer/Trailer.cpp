#include "Vehicles/Trailer/Trailer.h"

#define LOCTEXT_NAMESPACE "DFVehicles"

ATrailerCab::ATrailerCab()
{
	DisplayName = LOCTEXT("TrailerCab", "Trailer cab");
	NitroAccel = 0.f;   // RageGame.TrailerCab: never a turbo
	AccelTime = 9.f;   // pulling 1500 kg
	CameraDistance = 900.f;
}

ATrailer::ATrailer()
{
	PetrolCapPosition = FVector(166.5f, 0.f, 68.75f);   // TrailerX PetrolCapPos (74, 0, 30.6), PetrolCapRadius 7
	PetrolCapRadius = 15.75f;
	DisplayName = LOCTEXT("Trailer", "Bomb trailer");
	bDriveable = false;
	BodyMeshName = TEXT("Rage/TrailerMesh");
	BoxCenter = FVector(0.f, 0.f, 40.f);
	BoxExtent = FVector(170.f, 80.f, 80.f);
	MassKg = 1500.f;                   // handling TRAILER Mass 1500
	CenterOfMass = FVector(0.f, 0.f, -30.f);
	Armour = 0.5f;
	SuspensionTravel = 25.f;
	SuspensionDamping = 0.5f;
	// Trailer.COL; free-rolling, no steering
	Wheels = {
		DF::RageWheel(FVector(101.4f, 59.1f, -78.8f), 43.1f, false, false, TEXT("Rage/TrailerWheelR")),
		DF::RageWheel(FVector(101.4f, -59.7f, -78.8f), 43.1f, false, false, TEXT("Rage/TrailerWheelL")),
		DF::RageWheel(FVector(-88.3f, 59.1f, -78.8f), 43.1f, false, false, TEXT("Rage/TrailerWheelR")),
		DF::RageWheel(FVector(-88.3f, -59.7f, -78.8f), 43.1f, false, false, TEXT("Rage/TrailerWheelL")),
	};
}

#undef LOCTEXT_NAMESPACE
