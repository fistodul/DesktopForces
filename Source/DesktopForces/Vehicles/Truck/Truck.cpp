#include "Vehicles/Truck/Truck.h"

#define LOCTEXT_NAMESPACE "DFVehicles"

ATruck::ATruck()
{
	EngineSounds = TEXT("TruckSFX");   // EngineSound / EngineStartSound / EngineStopSound
	PetrolCapPosition = FVector(63.f, -163.7f, -128.25f);   // PetrolCapPos (28, -72.75, -57), PetrolCapRadius 12
	PetrolCapRadius = 27.f;
	NitroAccel = 0.f;                // TurboTruck (nitroScale 0.11, 40 m/s) wasn't on the servers: no nitro
	NitroMaxSpeed = 4000.f;
	DisplayName = LOCTEXT("Truck", "Truck");
	BodyMeshName = TEXT("Rage/TruckMesh");
	BoxCenter = FVector(0.f, 0.f, 20.f);
	BoxExtent = FVector(420.f, 110.f, 90.f);
	MassKg = 3000.f;
	TopSpeedKmh = 50.f * DF::Mph;
	AccelTime = 9.f;
	SteerLock = 35.f;                  // Lock 60 is a lot for a 6x6 at speed
	Armour = 0.78f;
	SuspensionTravel = 35.f;
	SuspensionDamping = 0.5f;
	// truck.COL: six wheels, front ones steer, all driven
	Wheels = {
		DF::RageWheel(FVector(328.1f, 114.f, -119.3f), 68.1f, true, true, TEXT("Rage/TruckWheelR")),
		DF::RageWheel(FVector(328.1f, -114.f, -119.3f), 68.1f, true, true, TEXT("Rage/TruckWheelL")),
		DF::RageWheel(FVector(-120.9f, 114.f, -119.1f), 68.1f, false, true, TEXT("Rage/TruckWheelR")),
		DF::RageWheel(FVector(-120.9f, -114.f, -119.1f), 68.1f, false, true, TEXT("Rage/TruckWheelL")),
		DF::RageWheel(FVector(-286.f, 114.f, -119.1f), 68.1f, false, true, TEXT("Rage/TruckWheelR")),
		DF::RageWheel(FVector(-286.f, -114.f, -119.1f), 68.1f, false, true, TEXT("Rage/TruckWheelL")),
	};
	SeatPositions = { FVector(182.8f, -70.3f, 68.f), FVector(182.8f, 70.3f, 68.f), FVector(0.f, 0.f, 123.75f), FVector(-154.7f, 0.f, 123.75f), FVector(-337.5f, 0.f, 123.75f) };
	SeatSits = { true, true };   // aSeatsSit
	DoorPositions = { FVector(196.9f, -270.f, 0.f), FVector(196.9f, 270.f, 0.f), FVector(-506.f, 0.f, 0.f) };
	// handling.hnd2: GraphTorque / GraphRPM, Ratios, ReverseRatio, FinalDrive
	SetHandling({ 530.f, 530.f, 530.f, 530.f, 530.f, 530.f, 530.f, 540.f, 90.f, 0.f },
		{ 1000.f, 2000.f, 2800.f, 3500.f, 4200.f, 5000.f, 5700.f, 6200.f, 6700.f, 7000.f },
		{ 4.f, 3.f, 2.2f, 1.f }, 3.f, 13.f);
	DoorSeats = DF::DoorSeats({ { 0 }, { 0, 1 }, { 4, 3, 2, 0, 1 } });   // front seats near the driver, the bed from the back
	CameraDistance = 742.5f;           // NearCamera 330
}

#undef LOCTEXT_NAMESPACE
