#include "Vehicles/Buggy/MFVehicle_Buggy.h"

#define LOCTEXT_NAMESPACE "MFVehicles"

AMFVehicle_Buggy::AMFVehicle_Buggy()
{
	EngineSounds = TEXT("BuggySFX");   // EngineSound / EngineStartSound / EngineStopSound
	PetrolCapPosition = FVector(-63.f, 1.7f, 61.4f);   // PetrolCapPos (-28, 0.75, 27.3), PetrolCapRadius 7
	PetrolCapRadius = 15.75f;
	NitroAccel = 930.f;              // TurboCars nitroScale (x 60 Hz, m -> cm)
	NitroMaxSpeed = 5000.f;           // TurboCars MaxSpeed 50 m/s
	DisplayName = LOCTEXT("Buggy", "Buggy");
	BodyMeshName = TEXT("Rage/BuggyMesh");
	BoxCenter = FVector(9.f, 0.f, 60.f);
	BoxExtent = FVector(200.f, 75.f, 40.f);
	MassKg = 800.f;                    // handling Mass 800
	TopSpeedKmh = 70.f * MF::Mph;          // MaxSpeed 70
	AccelTime = 5.f;
	SteerLock = 40.f;                  // Lock 40
	Armour = 0.367f;
	SuspensionTravel = 30.f;
	// Buggy.COL wheel spheres; rear-wheel drive (DriveType 1)
	Wheels = {
		MF::RageWheel(FVector(202.6f, 71.5f, 15.4f), 41.f, true, false, TEXT("Rage/BuggyWheelFR")),
		MF::RageWheel(FVector(202.6f, -72.4f, 15.4f), 41.f, true, false, TEXT("Rage/BuggyWheelFL")),
		MF::RageWheel(FVector(-85.f, 91.5f, 27.1f), 52.f, false, true, TEXT("Rage/BuggyWheelBR")),
		MF::RageWheel(FVector(-85.f, -92.4f, 27.1f), 52.f, false, true, TEXT("Rage/BuggyWheelBL")),
	};
	SeatPositions = { FVector(20.25f, 0.f, 85.5f), FVector(-157.5f, 0.f, 162.f) };          // aSeatsPos
	DoorPositions = { FVector(0.f, 135.f, 56.f), FVector(-315.f, 0.f, 67.5f), FVector(0.f, -135.f, 67.5f) };   // aDoorsPos
	CameraDistance = 562.5f;           // NearCamera 250
}

#undef LOCTEXT_NAMESPACE
