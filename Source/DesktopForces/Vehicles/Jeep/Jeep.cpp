#include "Vehicles/Jeep/Jeep.h"
#include "Vehicles/DFVehicleTurret.h"

#define LOCTEXT_NAMESPACE "DFVehicles"

AJeep::AJeep()
{
	DisplayName = LOCTEXT("Jeep", "Jeep");
	BodyMeshName = TEXT("jeep/jeepMesh");
	BoxCenter = FVector(0.f, 0.f, 70.f);
	BoxExtent = FVector(225.f, 100.f, 45.f);     // jeepMesh 476 x 226 cm
	MassKg = 1000.f;                             // handling JEEP Mass 1000
	TopSpeedKmh = 60.f * 1.609344f;              // MaxSpeed 60 (mph)
	AccelTime = 6.f;
	SteerLock = 40.f;                            // Lock 60 is the full lock; drives like the Humvee's 40
	Armour = 0.68f;
	SuspensionTravel = 30.f;
	SuspensionDamping = 0.45f;
	EngineStartSound = TEXT("Sounds/jeep/engine_start");
	EngineLoopSound = TEXT("Sounds/jeep/Engine");
	EngineStopSound = TEXT("Sounds/jeep/engine_stop");
	// jeep.COL wheel spheres (x, y, z m -> X = z, Y = x, Z = y cm); four-wheel drive (DriveType 2)
	auto Wheel = [](const FVector& Pos, bool bSteer)
	{
		FDFWheelSetup W;
		W.Position = Pos;
		W.Radius = 45.8f;
		W.bSteer = bSteer;
		W.bDrive = true;
		W.Mesh = TEXT("jeep/JeepWheelMesh");
		W.BlownMesh = TEXT("jeep/JeepWheelDamagedMesh");
		return W;
	};
	Wheels = { Wheel(FVector(140.f, 81.5f, 45.f), true), Wheel(FVector(140.f, -82.4f, 45.f), true),
		Wheel(FVector(-135.f, 81.5f, 31.1f), false), Wheel(FVector(-135.f, -82.4f, 31.1f), false) };
	// aSeatsPos (DriverSeat 1 first, the gunner standing at the back last), aDoorsPos
	SeatPositions = { FVector(9.f, -56.25f, 112.5f), FVector(9.f, 56.25f, 112.5f), FVector(-67.5f, -11.25f, 180.f) };
	DoorPositions = { FVector(22.5f, 148.5f, 57.4f), FVector(22.5f, -148.5f, 57.4f), FVector(-299.25f, 0.f, 57.4f) };
	PetrolCapPosition = FVector(-60.75f, -105.75f, 29.25f);   // PetrolCapPos (-27, -47, 13)
	PetrolCapRadius = 9.f;                                       // PetrolCapRadius 4
	CameraDistance = 562.5f;                                     // NearCamera 250
	NitroAccel = 0.f;                                            // TurboJeep (0.15, 30 m/s) wasn't on the servers

	Gun = CreateDefaultSubobject<UDFVehicleTurret>(TEXT("Gun"));
	Gun->GunnerSeat = 2;                                         // hmgSeat 2
	Gun->TurretPivot = FVector(-51.75f, -3.4f, 157.5f);         // vehicleWeapPos (-23, -1.5, 70)
	Gun->GunOffset = FVector::ZeroVector;                        // vehWeapFake sits at vehicleWeapPos
	Gun->MuzzleOffset = FVector(107.f, 0.f, 0.f);                // jeepWeapMesh barrel end X 47.6
	Gun->PitchMin = -60.f;                                       // aSeatsRotLimit(2) Pitch 16384
	Gun->PitchMax = 60.f;
	Gun->TurnRate = 720.f;                                       // the gunner's own view
	Gun->TurretMeshName = TEXT("jeep/jeepWeapMesh");             // vehWeapFake Mesh
	Gun->FireInterval = 0.117f;                                  // MachineGun
	Gun->MaxClipAmmo = 3000;                                     // vehWeap MaxClipAmmo 3000
	Gun->ReloadTime = 4.f;
	Gun->Spread = 0.03f;                                         // Accuracy 0.3
	Gun->Damage = 20.f;                                          // MachineGun HitDamage
	Gun->MaxRange = 22500.f;
	Gun->ClassicFireSound = TEXT("jeep.jeepWeapFire");
	Gun->ClassicReloadSound = TEXT("jeep.hmgReload");
}

#undef LOCTEXT_NAMESPACE
