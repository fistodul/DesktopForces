#include "Vehicles/MFVehicles.h"

#define LOCTEXT_NAMESPACE "MFVehicles"

namespace
{
	FMFWheelSetup Wheel(const FVector& Pos, float Radius, bool bSteer, bool bDrive, const TCHAR* Mesh)
	{
		FMFWheelSetup W;
		W.Position = Pos;
		W.Radius = Radius;
		W.bSteer = bSteer;
		W.bDrive = bDrive;
		W.Mesh = Mesh;
		return W;
	}
	constexpr float Mph = 1.609344f;   // handling MaxSpeed is in mph
}

AMFVehicle_Buggy::AMFVehicle_Buggy()
{
	DisplayName = LOCTEXT("Buggy", "Buggy");
	BodyMeshName = TEXT("Rage/BuggyMesh");
	BoxCenter = FVector(9.f, 0.f, 60.f);
	BoxExtent = FVector(200.f, 75.f, 40.f);
	MassKg = 800.f;                    // handling Mass 800
	TopSpeedKmh = 70.f * Mph;          // MaxSpeed 70
	AccelTime = 5.f;
	SteerLock = 40.f;                  // Lock 40
	Armour = 0.367f;
	SuspensionTravel = 30.f;
	// Buggy.COL wheel spheres; rear-wheel drive (DriveType 1)
	Wheels = {
		Wheel(FVector(202.6f, 71.5f, 15.4f), 41.f, true, false, TEXT("Rage/BuggyWheelFR")),
		Wheel(FVector(202.6f, -72.4f, 15.4f), 41.f, true, false, TEXT("Rage/BuggyWheelFL")),
		Wheel(FVector(-85.f, 91.5f, 27.1f), 52.f, false, true, TEXT("Rage/BuggyWheelBR")),
		Wheel(FVector(-85.f, -92.4f, 27.1f), 52.f, false, true, TEXT("Rage/BuggyWheelBL")),
	};
	SeatPositions = { FVector(20.25f, 0.f, 85.5f), FVector(-157.5f, 0.f, 162.f) };          // aSeatsPos
	DoorPositions = { FVector(0.f, 135.f, 56.f), FVector(-315.f, 0.f, 67.5f), FVector(0.f, -135.f, 67.5f) };   // aDoorsPos
	CameraDistance = 562.5f;           // NearCamera 250
}

AMFVehicle_Humvee::AMFVehicle_Humvee()
{
	DisplayName = LOCTEXT("Humvee", "Humvee");
	BodyMeshName = TEXT("Rage/humveeMesh");
	BoxCenter = FVector(20.f, 0.f, 110.f);
	BoxExtent = FVector(240.f, 90.f, 60.f);
	MassKg = 2000.f;
	TopSpeedKmh = 50.f * Mph;
	AccelTime = 7.f;
	SteerLock = 40.f;
	Armour = 0.68f;
	SuspensionTravel = 30.f;
	SuspensionDamping = 0.45f;
	// humvee.COL; four-wheel drive (DriveType 2)
	Wheels = {
		Wheel(FVector(202.6f, 81.5f, 40.8f), 45.8f, true, true, TEXT("Rage/humwheelR")),
		Wheel(FVector(202.6f, -82.4f, 40.8f), 45.8f, true, true, TEXT("Rage/humwheelL")),
		Wheel(FVector(-161.8f, 81.5f, 31.1f), 45.8f, false, true, TEXT("Rage/humwheelR")),
		Wheel(FVector(-161.8f, -82.4f, 31.1f), 45.8f, false, true, TEXT("Rage/humwheelL")),
	};
	SeatPositions = { FVector(34.9f, 72.f, 125.f), FVector(34.9f, -72.f, 125.f), FVector(-112.5f, -54.f, 248.6f), FVector(-112.5f, 54.f, 248.6f) };
	DoorPositions = { FVector(22.5f, 148.5f, 57.4f), FVector(22.5f, -148.5f, 57.4f), FVector(-299.f, 0.f, 57.4f) };
	CameraDistance = 562.5f;
}

AMFVehicle_Truck::AMFVehicle_Truck()
{
	DisplayName = LOCTEXT("Truck", "Truck");
	BodyMeshName = TEXT("Rage/TruckMesh");
	BoxCenter = FVector(0.f, 0.f, 20.f);
	BoxExtent = FVector(420.f, 110.f, 90.f);
	MassKg = 3000.f;
	TopSpeedKmh = 50.f * Mph;
	AccelTime = 9.f;
	SteerLock = 35.f;                  // Lock 60 is a lot for a 6x6 at speed
	Armour = 0.78f;
	SuspensionTravel = 35.f;
	SuspensionDamping = 0.5f;
	// truck.COL: six wheels, front ones steer, all driven
	Wheels = {
		Wheel(FVector(328.1f, 114.f, -119.3f), 68.1f, true, true, TEXT("Rage/TruckWheelR")),
		Wheel(FVector(328.1f, -114.f, -119.3f), 68.1f, true, true, TEXT("Rage/TruckWheelL")),
		Wheel(FVector(-120.9f, 114.f, -119.1f), 68.1f, false, true, TEXT("Rage/TruckWheelR")),
		Wheel(FVector(-120.9f, -114.f, -119.1f), 68.1f, false, true, TEXT("Rage/TruckWheelL")),
		Wheel(FVector(-286.f, 114.f, -119.1f), 68.1f, false, true, TEXT("Rage/TruckWheelR")),
		Wheel(FVector(-286.f, -114.f, -119.1f), 68.1f, false, true, TEXT("Rage/TruckWheelL")),
	};
	SeatPositions = { FVector(182.8f, -70.3f, 68.f), FVector(182.8f, 70.3f, 68.f), FVector(0.f, 0.f, 123.75f), FVector(-154.7f, 0.f, 123.75f), FVector(-337.5f, 0.f, 123.75f) };
	DoorPositions = { FVector(196.9f, -270.f, 0.f), FVector(196.9f, 270.f, 0.f), FVector(-506.f, 0.f, 0.f) };
	CameraDistance = 742.5f;           // NearCamera 330
}

AMFVehicle_Armadillo::AMFVehicle_Armadillo()
{
	DisplayName = LOCTEXT("Armadillo", "Armadillo APC");
	BodyMeshName = TEXT("Rage/ArmadilloMesh");
	BoxCenter = FVector(30.f, 0.f, 40.f);
	BoxExtent = FVector(330.f, 150.f, 80.f);
	MassKg = 2300.f;
	TopSpeedKmh = 30.f * Mph;
	AccelTime = 8.f;
	SteerLock = 40.f;
	Armour = 0.84f;
	SuspensionTravel = 25.f;
	SuspensionDamping = 0.5f;
	Wheels = {
		Wheel(FVector(270.9f, 139.9f, -66.1f), 67.9f, true, true, TEXT("Rage/ArmadilloWheelR")),
		Wheel(FVector(270.9f, -140.f, -66.1f), 67.9f, true, true, TEXT("Rage/ArmadilloWheelL")),
		Wheel(FVector(-185.9f, 139.9f, -66.1f), 67.9f, false, true, TEXT("Rage/ArmadilloWheelR")),
		Wheel(FVector(-185.9f, -140.f, -66.1f), 67.9f, false, true, TEXT("Rage/ArmadilloWheelL")),
	};
	SeatPositions = { FVector(112.5f, 0.f, 58.5f), FVector(0.f, 0.f, 180.f) };
	DoorPositions = { FVector(135.f, 225.f, 0.f), FVector(135.f, -225.f, 0.f), FVector(-337.5f, 0.f, 0.f), FVector(0.f, 0.f, 180.f) };
	CameraDistance = 562.5f;
}

#undef LOCTEXT_NAMESPACE
