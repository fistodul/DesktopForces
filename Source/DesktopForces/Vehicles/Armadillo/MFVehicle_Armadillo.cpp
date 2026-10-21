#include "Vehicles/Armadillo/MFVehicle_Armadillo.h"

#define LOCTEXT_NAMESPACE "MFVehicles"

AMFVehicle_Armadillo::AMFVehicle_Armadillo()
{
	EngineSounds = TEXT("ArmadilloSFX");   // EngineSound / EngineStartSound / EngineStopSound
	PetrolCapPosition = FVector(-123.75f, -51.75f, 102.5f);   // PetrolCapPos (-55, -23, 45.6), PetrolCapRadius 7
	PetrolCapRadius = 15.75f;
	NitroAccel = 600.f;              // TurboCars nitroScale (x 60 Hz, m -> cm)
	NitroMaxSpeed = 5000.f;           // TurboCars MaxSpeed 50 m/s
	DisplayName = LOCTEXT("Armadillo", "Armadillo APC");
	bBulletproof = true;               // an armoured personnel carrier: guns don't get through
	BodyMeshName = TEXT("Rage/ArmadilloMesh");
	BoxCenter = FVector(30.f, 0.f, 40.f);
	BoxExtent = FVector(330.f, 150.f, 80.f);
	MassKg = 2300.f;
	TopSpeedKmh = 30.f * MF::Mph;
	AccelTime = 8.f;
	SteerLock = 40.f;
	Armour = 0.84f;
	SuspensionTravel = 25.f;
	SuspensionDamping = 0.5f;
	Wheels = {
		MF::RageWheel(FVector(270.9f, 139.9f, -66.1f), 67.9f, true, true, TEXT("Rage/ArmadilloWheelR")),
		MF::RageWheel(FVector(270.9f, -140.f, -66.1f), 67.9f, true, true, TEXT("Rage/ArmadilloWheelL")),
		MF::RageWheel(FVector(-185.9f, 139.9f, -66.1f), 67.9f, false, true, TEXT("Rage/ArmadilloWheelR")),
		MF::RageWheel(FVector(-185.9f, -140.f, -66.1f), 67.9f, false, true, TEXT("Rage/ArmadilloWheelL")),
	};
	SeatPositions = { FVector(112.5f, 0.f, 58.5f), FVector(0.f, 0.f, 180.f) };
	DoorPositions = { FVector(135.f, 225.f, 0.f), FVector(135.f, -225.f, 0.f), FVector(-337.5f, 0.f, 0.f), FVector(0.f, 0.f, 180.f) };
	CameraDistance = 562.5f;
}

#undef LOCTEXT_NAMESPACE
