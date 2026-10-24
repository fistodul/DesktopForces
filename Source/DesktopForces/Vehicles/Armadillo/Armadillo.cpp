#include "Vehicles/Armadillo/Armadillo.h"

#define LOCTEXT_NAMESPACE "DFVehicles"

AArmadillo::AArmadillo() {
	EngineSounds = TEXT("ArmadilloSFX"); // EngineSound / EngineStartSound / EngineStopSound
	PetrolCapPosition = FVector(-123.75f, -51.75f, 102.5f); // PetrolCapPos (-55, -23, 45.6), PetrolCapRadius 7
	PetrolCapRadius = 15.75f;
	NitroAccel = 600.f; // TurboCars nitroScale (x 60 Hz, m -> cm)
	NitroMaxSpeed = 5000.f; // TurboCars MaxSpeed 50 m/s
	DisplayName = LOCTEXT("Armadillo", "Armadillo APC");
	bBulletproof = true; // an armoured personnel carrier: guns don't get through
	BodyMeshName = TEXT("Rage/ArmadilloMesh");
	BoxCenter = FVector(30.f, 0.f, 40.f);
	BoxExtent = FVector(330.f, 150.f, 80.f);
	MassKg = 2300.f;
	TopSpeedKmh = 30.f * DF::Mph;
	AccelTime = 8.f;
	SteerLock = 40.f;
	Armour = 0.84f;
	SuspensionTravel = 25.f;
	SuspensionDamping = 0.5f;
	Wheels = {
		DF::RageWheel(FVector(270.9f, 139.9f, -66.1f), 67.9f, true, true, TEXT("Rage/ArmadilloWheelR")),
		DF::RageWheel(FVector(270.9f, -140.f, -66.1f), 67.9f, true, true, TEXT("Rage/ArmadilloWheelL")),
		DF::RageWheel(FVector(-185.9f, 139.9f, -66.1f), 67.9f, false, true, TEXT("Rage/ArmadilloWheelR")),
		DF::RageWheel(FVector(-185.9f, -140.f, -66.1f), 67.9f, false, true, TEXT("Rage/ArmadilloWheelL")),
	};
	SeatPositions = { FVector(112.5f, 0.f, 58.5f), FVector(0.f, 0.f, 180.f) };
	DoorPositions = { FVector(135.f, 225.f, 0.f), FVector(135.f, -225.f, 0.f), FVector(-337.5f, 0.f, 0.f), FVector(0.f, 0.f, 180.f) };
	// handling.hnd2: GraphTorque / GraphRPM, Ratios, ReverseRatio, FinalDrive
	SetHandling({ 540.f, 540.f, 543.f, 543.f, 543.f, 543.f, 543.f, 543.f, 40.f, 0.f },
		{ 1000.f, 2000.f, 2800.f, 3500.f, 4200.f, 5000.f, 5700.f, 6200.f, 6700.f, 7000.f },
		{ 4.33f, 3.09f, 2.35f, 1.f }, 3.f, 12.f);
	DoorSeats = DF::DoorSeats({ { 0 }, { 0 }, { 1, 0 }, { 1 } }); // aSeatsDoor(1)=2, aDoorsSeat(3)=1: the gunner through the back or the hatch
	CameraDistance = 562.5f;
}

#undef LOCTEXT_NAMESPACE
