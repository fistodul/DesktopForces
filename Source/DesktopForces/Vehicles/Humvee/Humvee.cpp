#include "Vehicles/Humvee/Humvee.h"

#define LOCTEXT_NAMESPACE "DFVehicles"

AHumvee::AHumvee() {
	EngineSounds = TEXT("HumveeSFX"); // EngineSound / EngineStartSound / EngineStopSound
	PetrolCapPosition = FVector(-213.75f, -123.75f, 79.9f); // PetrolCapPos (-95, -55, 35.5), PetrolCapRadius 7
	PetrolCapRadius = 15.75f;
	NitroAccel = 900.f; // TurboCars nitroScale (x 60 Hz, m -> cm)
	NitroMaxSpeed = 4000.f; // TurboCars MaxSpeed 40 m/s
	DisplayName = LOCTEXT("Humvee", "Humvee");
	BodyMeshName = TEXT("Rage/humveeMesh");
	BoxCenter = FVector(20.f, 0.f, 110.f);
	BoxExtent = FVector(240.f, 90.f, 60.f);
	MassKg = 2000.f;
	TopSpeedKmh = 50.f * DF::Mph;
	AccelTime = 7.f;
	SteerLock = 40.f;
	Armour = 0.68f;
	SuspensionTravel = 30.f;
	SuspensionDamping = 0.45f;
	// humvee.COL; four-wheel drive (DriveType 2). The wheels hang 12 cm below the COL spots:
	// handling RideHeight 0.7, the highest of MF's cars (buggy 0.4, truck 0.5), sat on the ground
	Wheels = {
		DF::RageWheel(FVector(202.6f, 81.5f, 28.8f), 45.8f, true, true, TEXT("Rage/humwheelR")),
		DF::RageWheel(FVector(202.6f, -82.4f, 28.8f), 45.8f, true, true, TEXT("Rage/humwheelL")),
		DF::RageWheel(FVector(-161.8f, 81.5f, 19.1f), 45.8f, false, true, TEXT("Rage/humwheelR")),
		DF::RageWheel(FVector(-161.8f, -82.4f, 19.1f), 45.8f, false, true, TEXT("Rage/humwheelL")),
	};
	// aSeatsPos; MF DriverSeat=1: the driver sits on the left (our seat 0 drives)
	SeatPositions = { FVector(34.9f, -72.f, 125.f), FVector(34.9f, 72.f, 125.f), FVector(-112.5f, -54.f, 248.6f), FVector(-112.5f, 54.f, 248.6f) };
	SeatSits = { true, true }; // aSeatsSit
	DoorPositions = { FVector(22.5f, 148.5f, 57.4f), FVector(22.5f, -148.5f, 57.4f), FVector(-299.f, 0.f, 57.4f) };
	// handling.hnd2: GraphTorque / GraphRPM, Ratios, ReverseRatio, FinalDrive
	SetHandling({ 540.f, 540.f, 543.f, 543.f, 543.f, 543.f, 543.f, 543.f, 40.f, 0.f },
		{ 1000.f, 2000.f, 2800.f, 3500.f, 4200.f, 5000.f, 5700.f, 6200.f, 6700.f, 7000.f },
		{ 4.33f, 3.09f, 2.35f, 1.f }, 3.f, 12.f);
	DoorSeats = DF::DoorSeats({ { 0, 1 }, { 0, 1 }, { 3, 2, 0, 1 } }); // front seats near the driver, aSeatsDoor(2) / (3)=2
	CameraDistance = 562.5f;
	FlagPosition = FVector(-236.25f, -85.5f, 214.9f); // FlagPos x 2.25
}

#undef LOCTEXT_NAMESPACE
