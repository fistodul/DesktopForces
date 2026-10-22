#include "Vehicles/Boat/Boat.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "DFVehicles"

ABoat::ABoat()
{
	DisplayName = LOCTEXT("Boat", "Boat");
	bBotsDrive = false;
	BodyMeshName = TEXT("boat/boatMesh");
	BoxCenter = FVector(0.f, 0.f, 45.f);
	BoxExtent = FVector(245.f, 100.f, 62.f);   // boatMesh 508 x 209 cm, keel 18 cm under the origin
	MassKg = 1000.f;                             // handling BOAT Mass 1000
	CenterOfMass = FVector(0.f, 0.f, -45.f);
	BuoyancyPercent = 50.f;                      // Buoyancy 50
	Armour = 0.68f;
	PetrolCapPosition = FVector(-213.75f, -123.75f, 79.9f);   // PetrolCapPos (-95, -55, 35.5) as the Humvee's
	PetrolCapRadius = 15.75f;
	EngineStartSound = TEXT("Sounds/boat/boatEngineStart");
	EngineLoopSound = TEXT("Sounds/boat/boatEngineRunning");
	EngineStopSound = TEXT("Sounds/HumveeSFX/EngineStop");
	// aSeatsPos (DriverSeat 1 first), aDoorsPos
	SeatPositions = { FVector(79.9f, 2.25f, 158.7f), FVector(-57.4f, -29.25f, 158.7f), FVector(-57.4f, 36.f, 158.7f) };
	SeatSits = { true, true, true };   // aSeatsSit
	DoorPositions = { FVector(-78.75f, 148.5f, 57.4f), FVector(67.5f, -148.5f, 57.4f), FVector(-78.75f, -148.5f, 57.4f), FVector(67.5f, 148.5f, 57.4f) };
	DoorSeats = DF::DoorSeats({ { 2, 1 }, { 0, 1 }, { 1 }, { 0 } });   // aSeatsDoor(0)=2 / (1)=1, aDoorsSeat(3)=1 (our seat 0 = MF DriverSeat 1)
	CameraDistance = 562.5f;
	NitroAccel = 0.f;
}

void ABoat::SimulateDrive(float Dt)
{
	// state Driving.Tick: power only with waterCollider (25 uu under the origin) in the water and
	// the boat itself above it
	const FTransform T = Body->GetComponentTransform();
	const float Keel = T.TransformPosition(FVector(0.f, 0.f, -56.25f) - BoxCenter).Z;
	const float Deck = T.TransformPosition(-BoxCenter).Z;
	const bool bAfloat = Keel < GetWaterPlaneZ() && Deck > GetWaterPlaneZ();
	const float Ticks = Dt * 60.f;
	const FVector Fwd = T.GetUnitAxis(EAxis::X);
	const FVector Vel = Body->GetPhysicsLinearVelocity();
	if (!bAfloat || !GetDriver())
	{
		AccelMultiplier = 0.f;
		return;
	}
	float Cap = 0.f;
	if (Throttle > 0.1f)
	{
		AccelMultiplier = AccelMultiplier < 0.f ? AccelMultiplier + 0.09f * Ticks : FMath::Min(AccelMultiplier + 0.004f * Ticks, 0.85f);
		Cap = MaxSpeedForward;
	}
	else if (Throttle < -0.1f)
	{
		AccelMultiplier = AccelMultiplier > 0.f ? AccelMultiplier - 0.09f * Ticks : FMath::Max(AccelMultiplier - 0.004f * Ticks, -0.2f);
		Cap = MaxSpeedBackward;
	}
	else
	{
		AccelMultiplier = FMath::Abs(AccelMultiplier) <= 0.06f * Ticks ? 0.f : AccelMultiplier - FMath::Sign(AccelMultiplier) * 0.06f * Ticks;
		Cap = MaxSpeedBackward;
	}
	// setTransVel(vel + vector(Rotation) * mult) each tick while under the cap: m/s -> cm/s
	if (Vel.Size() < Cap)
	{
		Body->AddForce(Fwd * AccelMultiplier * 60.f * 100.f * MassKg);
	}
	// setRotVel(rotVel +- Z * turnVelocityMultiplier): the rudder, reversed when backing
	if (FMath::Abs(Steer) > 0.1f)
	{
		const float Dir = FMath::Sign(Steer) * (Throttle < -0.1f ? -1.f : 1.f);
		Body->AddTorqueInRadians(T.GetUnitAxis(EAxis::Z) * Dir * TurnRate, NAME_None, true);
	}
}

#undef LOCTEXT_NAMESPACE
