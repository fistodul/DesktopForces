#include "Vehicles/Piper/MFVehicle_Piper.h"
#include "Core/MFProps.h"
#include "World/MFZoneVolume.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

#define LOCTEXT_NAMESPACE "MFVehicles"

namespace
{
	// piper defaultproperties (m/s, per 60 Hz tick)
	constexpr float HorizontalSpeedMax = 30.f;
	constexpr float PitchRotVel = 0.01f, RollRotVel = 0.01f, RollNoYaw = 3.f, YawRotVel = 0.01f;
	constexpr float MinHorizontalAirborneSpeed = 15.f, MinVerticalAirborneSpeed = 10.f;
	constexpr float SpeedChange = 0.1f, FallingRotForce = 0.01f;
	constexpr float MaxDownFreefall = 35.f, MaxDownAccelerating = 45.f, TakeoffSpeed = 10.f;
}

AMFVehicle_Piper::AMFVehicle_Piper()
{
	DisplayName = LOCTEXT("Piper", "Piper");
	bBotsDrive = false;                          // no flying bots
	BodyMeshName = TEXT("piper/piperA4Mesh");
	Liveries = { TEXT("piper/piperA4Mesh"), TEXT("piper/piperA1Mesh"), TEXT("piper/piperA3Mesh") };
	BoxCenter = FVector(-20.f, 0.f, 0.f);        // the fuselage (piper.COL spheres 0.35 - 0.65 m round) down to
	BoxExtent = FVector(330.f, 70.f, 75.f);       // the axles: the suspension rays start inside the box
	MassKg = 1000.f;                             // handling PIPER Mass 1000
	CenterOfMass = FVector(140.f, 0.f, -30.f);    // between the main gear and the nose wheel, low: it stands on all three
	TopSpeedKmh = 50.f * 1.609344f;              // MaxSpeed 50 (mph) on the ground
	AccelTime = 6.f;
	SteerLock = 40.f;                            // Lock 40
	Armour = 0.68f;
	NitroAccel = 0.f;                            // Crouch pitches it down
	BuoyancyPercent = 65.f;                      // Buoyancy 65
	PetrolCapPosition = FVector(-213.75f, -123.75f, 79.875f);   // PetrolCapPos
	PetrolCapRadius = 15.75f;
	CameraDistance = 1237.5f;                 // NearCamera 550 uu
	EngineStartSound = TEXT("Sounds/HumveeSFX/EngineStart");
	EngineLoopSound = TEXT("Sounds/HumveeSFX/Engine1");
	EngineStopSound = TEXT("Sounds/HumveeSFX/EngineStop");
	// piper.COL wheel spheres (x, y, z m -> X = z, Y = x, Z = y cm): the nose wheel(s) steer
	auto Wheel = [](const FVector& Pos, bool bSteer)
	{
		FMFWheelSetup W;
		W.Position = Pos;
		W.Radius = 20.f;
		W.bSteer = bSteer;
		W.bDrive = true;                         // DriveType 2
		W.Mesh = TEXT("piper/piperTire");
		W.BlownMesh = TEXT("piper/piperTireBlown");
		return W;
	};
	Wheels = { Wheel(FVector(305.f, 2.7f, -71.8f), true), Wheel(FVector(305.f, 1.7f, -71.8f), true),
		Wheel(FVector(58.3f, 208.6f, -71.8f), false), Wheel(FVector(58.3f, -214.5f, -71.8f), false) };
	SeatPositions = { FVector(135.9f, 0.f, 113.85f) };                                   // aSeatsPos(0)
	DoorPositions = { FVector(22.5f, -148.5f, 57.375f), FVector(22.5f, 148.5f, 57.375f) };   // aDoorsPos
	Propeller = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Propeller"));
	Propeller->SetupAttachment(Body);
	Propeller->SetRelativeLocation(FVector(270.f, -2.25f, 90.f) - BoxCenter);   // propellerPos
	Propeller->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Propeller->SetCanEverAffectNavigation(false);
	Propeller->SetCastShadow(false);
}

AMFVehicle_PiperTs::AMFVehicle_PiperTs()
{
	DisplayName = LOCTEXT("PiperTs", "Piper (two seats)");
	BodyMeshName = TEXT("piper/piperA4TsMesh");
	Liveries = { TEXT("piper/piperA4TsMesh"), TEXT("piper/piperA1TsMesh"), TEXT("piper/piperA3TsMesh") };
	// aSeatsPos: DriverSeat 1 at the front, the passenger behind facing back; aDoorsPos
	SeatPositions = { FVector(135.f, 0.f, 135.f), FVector(22.5f, 0.f, 135.f) };
	DoorPositions = { FVector(33.75f, -135.f, 101.25f), FVector(-202.5f, -135.f, 101.25f), FVector(33.75f, 135.f, 101.25f), FVector(-202.5f, 135.f, 101.25f) };
}

void AMFVehicle_Piper::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AMFVehicle_Piper, Livery, COND_InitialOnly);
}

void AMFVehicle_Piper::BeginPlay()
{
	if (HasAuthority())
	{
		Livery = uint8(FMath::RandRange(0, Liveries.Num() - 1));
	}
	BodyMeshName = Liveries.IsValidIndex(Livery) ? Liveries[Livery] : BodyMeshName;
	Super::BeginPlay();
	MF::ApplyPropMesh(Propeller, TEXT("piper/piperPropeller"));
}

void AMFVehicle_Piper::OnRep_Livery()
{
	if (Liveries.IsValidIndex(Livery))
	{
		BodyMeshName = Liveries[Livery];
		MF::ApplyPropMesh(BodyMesh, *BodyMeshName);
	}
}

void AMFVehicle_Piper::Tick(float DeltaSeconds)
{
	const float Ticks = DeltaSeconds * 60.f;
	const bool bPilot = GetDriver() != nullptr && !IsBlownUp();
	EnginePitch = FMath::Clamp(EnginePitch + (bPilot ? 1.f : -1.f) * 0.15f * Ticks, 0.f, 60.f);
	const float Spin = bPilot ? FMath::Max(EnginePitch - 15.f, 0.f) : EnginePitch;
	PropellerRoll = FMath::Fmod(PropellerRoll + Spin * 100.f * Ticks * 360.f / 65536.f, 360.f);   // propellerSpinAmountMultiplier 100
	Propeller->SetRelativeRotation(FRotator(0.f, 0.f, PropellerRoll));
	Super::Tick(DeltaSeconds);
}

bool AMFVehicle_Piper::WheelsDown() const
{
	// isWheelTouching(1), (2), (3)
	int32 Down = 0;
	for (int32 I = 1; I < LastCompression.Num(); ++I)
	{
		Down += LastCompression[I] > 0.f;
	}
	return Down >= 3;
}

void AMFVehicle_Piper::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation,
	FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	// notifyCollision: on its wheels it has landed, on anything else it crashed
	if (!HasAuthority() || !bAirborne || bIgnoreCollision)
	{
		return;
	}
	bAirborne = false;
	bCrashed = !WheelsDown();
	AirSpeed = 0.f;
}

void AMFVehicle_Piper::SimulateDrive(float Dt)
{
	const float Ticks = Dt * 60.f;
	if (bCrashed && WheelsDown())
	{
		bCrashed = false;   // back on its wheels: ready to take off again
	}
	if (bIgnoreCollision && !WheelsDown())
	{
		bIgnoreCollision = false;
	}
	if (bAirborne && IsFloating())
	{
		bAirborne = false;   // zoneChange into water
		bCrashed = true;
	}
	if (!GetDriver() || EnginePitch < 60.f)
	{
		bAirborne = false;
		return;
	}
	const FTransform T = Body->GetComponentTransform();
	const FVector X = T.GetUnitAxis(EAxis::X), Y = T.GetUnitAxis(EAxis::Y), Up = T.GetUnitAxis(EAxis::Z);
	FRotator NoRoll = T.Rotator();
	NoRoll.Roll = 0.f;
	const FVector RollY = FRotationMatrix(NoRoll).GetUnitAxis(EAxis::Y);
	const bool bAccel = Throttle > 0.1f;
	FVector RotVel = FVector::ZeroVector;   // MF rotVel; Rage spins the other way round, so it is negated below
	if (bHandbrake && !(bAirborne && AirSpeed < MinHorizontalAirborneSpeed))
	{
		RotVel = Y * (PitchRotVel + (WheelsDown() ? 0.03f : 0.f));   // nose up
	}
	else if (bNitro)
	{
		RotVel = -Y * PitchRotVel;                                      // nose down
	}
	if (Steer > 0.1f)
	{
		RotVel += X * RollRotVel - Up * YawRotVel;
	}
	else if (Steer < -0.1f)
	{
		RotVel += -X * RollRotVel + Up * YawRotVel;
	}
	if (bDriverFire)
	{
		RotVel += -X * RollRotVel * RollNoYaw;
	}
	else if (bDriverAltFire)
	{
		RotVel += X * RollRotVel * RollNoYaw;
	}
	const FVector Vel = Body->GetPhysicsLinearVelocity() / 100.f;   // m/s
	const bool bFlat = FMath::Abs(Vel.X) + FMath::Abs(Vel.Y) > FMath::Abs(Vel.Z) * 1.5f;
	if (!bAirborne)
	{
		if (!bCrashed && bHandbrake && bAccel && Vel.Size() > TakeoffSpeed && bFlat)
		{
			bAirborne = true;
			bIgnoreCollision = true;
			AirSpeed = Vel.Size();
		}
	}
	else if (bFlat)
	{
		if (AirSpeed < MinHorizontalAirborneSpeed)
		{
			RotVel += -RollY * FallingRotForce;   // stalling: the nose drops
		}
		AirSpeed = bAccel ? AirSpeed + (AirSpeed < HorizontalSpeedMax ? 1.f : -1.f) * SpeedChange * Ticks : AirSpeed * (1.f - 0.01f * Ticks);
	}
	else
	{
		if (AirSpeed < MinVerticalAirborneSpeed)
		{
			RotVel += -RollY * FallingRotForce;
		}
		if (Vel.Z > 0.f)
		{
			AirSpeed *= 1.f - (bAccel ? 0.01f : 0.11f) * Ticks;   // climbing steeply bleeds speed
		}
		else if (AirSpeed < 10.f)
		{
			AirSpeed *= 1.f + (bAccel ? 0.4f : 0.2f) * Ticks;
		}
		else if (AirSpeed < MaxDownFreefall || (bAccel && AirSpeed < MaxDownAccelerating))
		{
			AirSpeed *= 1.f + 0.01f * Ticks;
		}
	}
	if (bAirborne)
	{
		Body->SetPhysicsLinearVelocity(X * AirSpeed * 100.f);   // setTransVel(rotXaxis * smallSpeed)
	}
	else if (bAccel)
	{
		// the propeller pulls and the wheels roll free: none of the cars' rolling resistance
		// (AMFVehicle::SimulateWheels), so it gets to MaxSpeed and past TakeoffSpeed
		Body->AddForce(X * MassKg * 0.3f * FMath::Max(0.f, FVector::DotProduct(Vel * 100.f, X)));
	}
	Body->SetPhysicsAngularVelocityInRadians(Body->GetPhysicsAngularVelocityInRadians() - RotVel * Ticks);
}

#undef LOCTEXT_NAMESPACE
