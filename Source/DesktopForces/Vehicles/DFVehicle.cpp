#include "Vehicles/DFVehicle.h"
#include "Materials/MaterialInterface.h"
#include "Game/DFGameState.h"
#include "Vehicles/DFVehicles.h"
#include "Vehicles/Boat/Boat.h"
#include "Vehicles/Jeep/Jeep.h"
#include "Vehicles/DFVehicleTurret.h"
#include "DesktopForces.h"
#include "Core/DFProps.h"
#include "Player/DFCharacter.h"
#include "Vehicles/VehicleFrag.h"
#include "Vehicles/Heli/Heli.h"
#include "Core/DFGameSettings.h"
#include "Weapons/DFWeapon.h"
#include "World/DFMapActors.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Core/DFContent.h"
#include "Components/AudioComponent.h"
#include "Components/SphereComponent.h"
#include "Weapons/DFProjectile.h"
#include "Game/DFGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "DFVehicle"

UDFDamage_RunOver::UDFDamage_RunOver() {
	DeathVerb = LOCTEXT("RanOver", "ran over");
}

ADFVehicle::ADFVehicle() {
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	SetReplicatingMovement(true);
	AutoPossessAI = EAutoPossessAI::Disabled;

	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->SetCollisionProfileName(TEXT("PhysicsActor"));
	Body->SetNotifyRigidBodyCollision(true);
	Body->BodyInstance.bUseCCD = true;
	// the navmesh is baked with the map: a car moving (or appearing at a generator) would only
	// mark it dirty ("NAVMESH NEEDS TO BE REBUILT"); bots steer round vehicles themselves
	Body->SetCanEverAffectNavigation(false);
	RootComponent = Body;

	// bullets and knives hit the model itself, not the physics box (the "Weapon" trace channel,
	// ECC_GameTraceChannel2): gaps stay gaps, the petrol cap can be aimed at
	Body->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(Body);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BodyMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	BodyMesh->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
	BodyMesh->BodyInstance.bAutoWeld = false; // its own query body following the car, not part of the physics one
	BodyMesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
		BodyMesh->SetStaticMesh(Cube.Object); // placeholder box until the MF body is converted

	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(Body);
	CameraArm->bUsePawnControlRotation = true;
	CameraArm->bDoCollisionTest = true;
	CameraArm->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraArm);
}

void ADFVehicle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADFVehicle, Health);
	DOREPLIFETIME(ADFVehicle, Driver);
	DOREPLIFETIME(ADFVehicle, BlownWheels);
	DOREPLIFETIME(ADFVehicle, Passengers);
}

void ADFVehicle::BeginPlay() {
	Super::BeginPlay();
	if (!FlagPosition.IsZero() && GetNetMode() != NM_DedicatedServer)
		GetWorldTimerManager().SetTimer(FlagTimer, this, &ADFVehicle::UpdateFlag, 0.5f, true, 0.1f);
	Body->SetBoxExtent(BoxExtent);
	CameraArm->TargetArmLength = CameraDistance;
	// everything is laid out in MF body space (origin at the MF actor); the box is the root
	if (!DF::ApplyPropMesh(BodyMesh, *BodyMeshName, 1.f, -BoxCenter))
		BodyMesh->SetRelativeScale3D(BoxExtent / 50.f);
	for (const FDFWheelSetup& W : Wheels) {
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(Body);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->RegisterComponent();
		if (!DF::ApplyPropMesh(C, *W.Mesh, 1.f, W.Position - BoxCenter)) {
			C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
			C->SetRelativeLocation(W.Position - BoxCenter);
			C->SetRelativeRotation(FRotator(0.f, 0.f, 90.f));
			C->SetRelativeScale3D(FVector(W.Radius / 50.f, W.Radius / 50.f, 0.3f));
		}
		WheelMeshes.Add(C);
		// MF WheelCollision: a sphere round the tyre that only weapon traces see
		USphereComponent* S = NewObject<USphereComponent>(this);
		S->SetupAttachment(C);
		S->SetSphereRadius(W.Radius);
		S->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		S->SetCollisionResponseToAllChannels(ECR_Ignore);
		S->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
		S->BodyInstance.bAutoWeld = false;
		S->SetCanEverAffectNavigation(false);
		S->RegisterComponent();
		WheelColliders.Add(S);
	}
	if (bDriveable || !PetrolCapPosition.IsZero()) {
		PetrolCapCollider = NewObject<USphereComponent>(this);
		PetrolCapCollider->SetupAttachment(Body);
		PetrolCapCollider->SetRelativeLocation(PetrolCapPosition - BoxCenter);
		PetrolCapCollider->SetSphereRadius(PetrolCapRadius);
		PetrolCapCollider->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		PetrolCapCollider->SetCollisionResponseToAllChannels(ECR_Ignore);
		PetrolCapCollider->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
		PetrolCapCollider->BodyInstance.bAutoWeld = false;
		PetrolCapCollider->SetCanEverAffectNavigation(false);
		PetrolCapCollider->RegisterComponent();
	}
	WheelDamage.Init(0, Wheels.Num());
	ReInflateTimers.SetNum(Wheels.Num());
	LastCompression.Init(0.f, Wheels.Num());
	WheelSpin.Init(0.f, Wheels.Num());
	Body->OnComponentHit.AddDynamic(this, &ADFVehicle::OnBodyHit);
	if (!EngineSounds.IsEmpty()) {
		auto Default = [this](FString& Path, const TCHAR* Name) { if (Path.IsEmpty())  Path = FString::Printf(TEXT("Sounds/%s/%s"), *EngineSounds, Name); };
		Default(EngineStartSound, TEXT("EngineStart"));
		Default(EngineLoopSound, TEXT("Engine1"));
		Default(EngineStopSound, TEXT("EngineStop"));
	}
	for (const ADFLegacyActor* W : ADFLegacyActor::FindAll(GetWorld(), TEXT("PhysicalWaterInfo")))
		if (const FString* H = W->Properties.Find(TEXT("WaterHeight")))
			WaterPlaneZ = FCString::Atof(**H) * 2.25f; // uu -> cm
	if (HasAuthority()) {
		const ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>();
		bTurbo = GM && GM->bTurboCars;
		Body->SetMassOverrideInKg(NAME_None, MassKg, true);
		Body->SetCenterOfMass(CenterOfMass);
		Body->SetLinearDamping(0.02f);
		Body->SetAngularDamping(0.6f);
		Body->SetSimulatePhysics(true);
	}
}

float ADFVehicle::GetSpeedKmh() const {
	return GetVelocity().Size() * 0.036f;
}

void ADFVehicle::DebugDump() const {
	FString Wheel;
	for (int32 i = 0; i < Wheels.Num(); ++i)
		Wheel += FString::Printf(TEXT(" %.0f"), LastCompression.IsValidIndex(i) ? LastCompression[i] : -1.f);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFVehicleDump), false, this);
	const FVector L = GetActorLocation();
	const bool bGround = GetWorld()->LineTraceSingleByChannel(Hit, L + FVector(0, 0, 200), L - FVector(0, 0, 1000), ECC_Visibility, Params);
	UE_LOG(LogDF, Log, TEXT("Vehicle %s at %s rot %s mass %.0f gravity %.0f awake %d sim %d ground %.1f (origin %.1f above) compression%s"),
		*GetName(), *L.ToCompactString(), *GetActorRotation().ToCompactString(), Body->GetMass(), GetWorld()->GetGravityZ(), Body->RigidBodyIsAwake(), Body->IsSimulatingPhysics(),
		bGround ? Hit.ImpactPoint.Z : -99999.f, bGround ? L.Z - Hit.ImpactPoint.Z : 0.f, *Wheel);
}

void ADFVehicle::SetDriveInput(float InThrottle, float InSteer, bool bInHandbrake) {
	Throttle = FMath::Clamp(InThrottle, -1.f, 1.f);
	Steer = FMath::Clamp(InSteer, -1.f, 1.f);
	bHandbrake = bInHandbrake;
	LastInputTime = GetWorld()->GetTimeSeconds();
	if (!HasAuthority())
		ServerDriveInput(Throttle, Steer, bHandbrake);
}

void ADFVehicle::ServerDriveInput_Implementation(float InThrottle, float InSteer, bool bInHandbrake) {
	SetDriveInput(InThrottle, InSteer, bInHandbrake);
}

void ADFVehicle::SetDriverFire(bool bFire, bool bAlt) {
	(bAlt ? bDriverAltFire : bDriverFire) = bFire;
	if (!HasAuthority())
		ServerSetDriverFire(bFire, bAlt);
}

void ADFVehicle::ServerSetDriverFire_Implementation(bool bFire, bool bAlt) {
	SetDriverFire(bFire, bAlt);
}

float ADFVehicle::GetEnginePitch() const {
	return FMath::Min(128.f, 56.f + 2.f * GetVelocity().Size() / 2.25f / 60.f) / 64.f;
}

void ADFVehicle::SetNitro(bool bOn) {
	bNitro = bOn;
	if (!HasAuthority())
		ServerSetNitro(bOn);
}

void ADFVehicle::ServerSetNitro_Implementation(bool bOn) {
	bNitro = bOn;
}

void ADFVehicle::Tick(float DeltaSeconds) {
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && !bBlownUp && Body->IsSimulatingPhysics()) {
		PrevVelocity = Body->GetPhysicsLinearVelocity();
		SimulateWheels(FMath::Min(DeltaSeconds, 0.05f));
	}
	if (GetNetMode() != NM_DedicatedServer) {
		UpdateWheelVisuals(DeltaSeconds);
		UpdateEngineSound();
	}
	// controllers send input only while keys are held: let go of everything otherwise
	if (!Driver || bLocked || GetWorld()->GetTimeSeconds() - LastInputTime > 0.15f) {
		Throttle = Steer = 0.f;
		bHandbrake = bLocked || (bDriveable && !Driver); // parked cars hold, towed trailers roll
	}
}

void ADFVehicle::ApplyWater(float Dt) {
	// Bill's physics water plane (PhysicalWaterInfo.SetWaterHeight): buoyancy at the box's bottom
	// corners and centre, equal to the weight with Buoyancy % of the body under; water drag
	Submerged = 0.f;
	if (WaterPlaneZ < -1e8f)
		return;
	const FTransform T = Body->GetComponentTransform();
	const FVector E = BoxExtent;
	const FVector Points[] = { FVector(E.X, E.Y, -E.Z), FVector(E.X, -E.Y, -E.Z), FVector(-E.X, E.Y, -E.Z), FVector(-E.X, -E.Y, -E.Z), FVector(0.f, 0.f, -E.Z) };
	const float Height = 2.f * E.Z;
	const float Weight = MassKg * -GetWorld()->GetGravityZ();
	const float Float = FMath::Max(BuoyancyPercent, 5.f) / 100.f;
	for (const FVector& Local : Points) {
		const FVector P = T.TransformPosition(Local);
		const float Depth = FMath::Clamp(WaterPlaneZ - P.Z, 0.f, Height);
		if (Depth > 0.f) {
			Body->AddForceAtLocation(FVector(0.f, 0.f, Weight / 5.f * Depth / (Float * Height)), P);
			Submerged += Depth / Height / 5.f;
		}
	}
	if (Submerged > 0.f) {
		Body->AddForce(-Body->GetPhysicsLinearVelocity() * MassKg * 1.5f * Submerged);
		Body->AddTorqueInRadians(-Body->GetPhysicsAngularVelocityInRadians() * MassKg * 20000.f * Submerged);
	}
}

void ADFVehicle::SimulateWheels(float Dt) {
	ApplyWater(Dt);
	if (!Driver && !Body->RigidBodyIsAwake() && FMath::IsNearlyZero(Throttle))
		return; // parked and settled: no traces, no forces
	const FTransform T = Body->GetComponentTransform();
	const FVector Up = T.GetUnitAxis(EAxis::Z);
	const FVector Fwd = T.GetUnitAxis(EAxis::X);
	const float N = FMath::Max(1, Wheels.Num());
	const float WheelMass = MassKg / N;
	const float RestLoad = MassKg * -GetWorld()->GetGravityZ() / N; // kg cm/s^2 per wheel (MF gravity is 2.2x UE's)
	const float K = SuspensionStiffness * RestLoad / SuspensionTravel; // per cm of compression
	const float C = SuspensionDamping * 2.f * FMath::Sqrt(K * WheelMass);
	const float TopSpeed = TopSpeedKmh / 0.036f; // cm/s
	const float EngineForce = MassKg * TopSpeed / AccelTime * 1.4f;
	int32 Driven = 0;
	for (const FDFWheelSetup& W : Wheels)
		Driven += W.bDrive;
	const float Speed = FVector::DotProduct(Body->GetPhysicsLinearVelocity(), Fwd);
	const float DriveForce = GearRatios.Num() ? GetDriveForce(Speed, Throttle) : Throttle * (Throttle < 0.f ? 0.5f : 1.f) * EngineForce;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFWheel), false, this);
	if (Driver)
		Params.AddIgnoredActor(Driver);
	int32 Grounded = 0;
	for (int32 i = 0; i < Wheels.Num(); ++i) {
		const FDFWheelSetup& W = Wheels[i];
		// a flat tyre (MF BlowOutWheel) runs on the rim: lower, little grip, little drive
		const bool bFlat = IsWheelBlown(i);
		// from a wheel radius above the travel's top: inside the body, so a car that bottomed
		// out (box on the ground, wheel tops below it) still finds the ground and lifts again
		const FVector Top = T.TransformPosition(W.Position - BoxCenter + FVector(0.f, 0.f, SuspensionTravel * 0.5f + W.Radius));
		const float RayLen = SuspensionTravel + W.Radius * (bFlat ? 1.75f : 2.f);
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Top, Top - Up * RayLen, ECC_Visibility, Params)) {
			LastCompression[i] = 0.f;
			AirborneWheels |= 1 << i;
			continue;
		}
		if (AirborneWheels & (1 << i)) {
			AirborneWheels &= ~(1 << i);
			const float Fall = -Body->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint).Z;
			if (Fall > 200.f && GetWorld()->GetTimeSeconds() >= WheelThumpUntil) {
				WheelThumpUntil = GetWorld()->GetTimeSeconds() + 0.25f;
				MulticastWheelThump(FMath::Min(Fall / 900.f, 1.f));
			}
		}
		++Grounded;
		const float Compression = FMath::Clamp(RayLen - Hit.Distance, 0.f, SuspensionTravel);
		const float CompVel = (Compression - LastCompression[i]) / FMath::Max(Dt, 1e-3f);
		LastCompression[i] = Compression;
		const float Fz = FMath::Max(0.f, K * Compression + C * CompVel);
		const FVector Contact = Hit.ImpactPoint;
		// wheel frame: steering turns the front ones (less at speed)
		const float SteerAngle = W.bSteer ? Steer * SteerLock * FMath::GetMappedRangeValueClamped(FVector2D(0.f, TopSpeed), FVector2D(1.f, 0.45f), FMath::Abs(Speed)) : 0.f;
		const FVector WFwd = Fwd.RotateAngleAxis(SteerAngle, Up);
		const FVector WRight = FVector::CrossProduct(Up, WFwd);
		const FVector V = Body->GetPhysicsLinearVelocityAtPoint(Contact);
		const float VLat = FVector::DotProduct(V, WRight);
		const float VLong = FVector::DotProduct(V, WFwd);
		// sideways grip, weaker on the rear with the handbrake pulled (slides; a parked car keeps it)
		const float GripHere = ((bHandbrake && Driver && !W.bSteer) ? Grip * 0.25f : Grip) * (bFlat ? 0.35f : 1.f);
		const float MaxFriction = 1.3f * Fz;
		float FLat = FMath::Clamp(-VLat * GripHere * WheelMass, -MaxFriction, MaxFriction);
		// engine, brakes, rolling resistance (strong when coasting: cars roll to a stop like MF's)
		float FLong = -VLong * (FMath::IsNearlyZero(Throttle) ? CoastDrag : RollingDrag) * WheelMass;
		const bool bBraking = !FMath::IsNearlyZero(Throttle) && FMath::Abs(Speed) > 60.f && FMath::Sign(Throttle) != FMath::Sign(Speed);
		// no throttle and next to no speed: held (a car left with its engine idling doesn't roll
		// away down a slope; MF's engine braking and clutch held them)
		const bool bHold = FMath::IsNearlyZero(Throttle) && FMath::Abs(Speed) < 150.f;
		if (bBraking || bHandbrake || bHold) {
			const float Brake = BrakeDecel * WheelMass * (bHandbrake || bHold ? 1.f : FMath::Abs(Throttle));
			FLong = -FMath::Sign(VLong) * FMath::Min(Brake, FMath::Abs(VLong) * WheelMass / FMath::Max(Dt, 1e-3f));
		} else if (W.bDrive && Driven > 0) {
			FLong += DriveForce / Driven * (bFlat ? 0.5f : 1.f);
		}
		FLong = FMath::Clamp(FLong, -MaxFriction * 1.2f, MaxFriction * 1.2f);
		Body->AddForceAtLocation(Up * Fz + WRight * FLat + WFwd * FLong, Contact);
		WheelSpin[i] += VLong / FMath::Max(W.Radius, 1.f) * Dt;
	}
	SimulateDrive(Dt);
	// TurboCars: crouch held in the driver's seat, on the ground, below the nitro top speed
	if (bTurbo && bNitro && Driver && Grounded > 0 && NitroAccel > 0.f && FMath::Abs(Speed) < NitroMaxSpeed)
		Body->AddForce(Fwd * MassKg * NitroAccel);
	// air drag caps the top speed: at TopSpeed (MF MaxSpeed) the engine just matches drag plus
	// rolling resistance; some downforce keeps it planted
	const FVector Vel = Body->GetPhysicsLinearVelocity();
	const float DragK = FMath::Max(EngineForce - RollingDrag * MassKg * TopSpeed, 0.1f * EngineForce) / (TopSpeed * TopSpeed);
	Body->AddForce(-Vel * Vel.Size() * DragK);
	if (Grounded > 0)
		Body->AddForce(-Up * Vel.SizeSquared() * DragK * 0.5f);
	// stuck on its side or roof: back on its wheels after 3 s (MF UpRightingForce)
	if (Up.Z < 0.3f && Vel.Size() < 150.f) {
		UpsideDownTime += Dt;
		if (UpsideDownTime > 3.f) {
			UpsideDownTime = 0.f;
			const FRotator Rot(0.f, GetActorRotation().Yaw, 0.f);
			Body->SetWorldLocationAndRotation(GetActorLocation() + FVector(0.f, 0.f, 120.f), Rot, false, nullptr, ETeleportType::TeleportPhysics);
			Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		}
	} else {
		UpsideDownTime = 0.f;
	}
}

void ADFVehicle::UpdateWheelVisuals(float DeltaSeconds) {
	const FVector Fwd = GetActorForwardVector();
	const float Speed = FVector::DotProduct(GetVelocity(), Fwd);
	for (int32 i = 0; i < WheelMeshes.Num() && i < Wheels.Num(); ++i) {
		const FDFWheelSetup& W = Wheels[i];
		if (!HasAuthority())
			WheelSpin[i] += Speed / FMath::Max(W.Radius, 1.f) * DeltaSeconds;
		// suspension drop when not compressed, steering, rolling
		const float Drop = HasAuthority() ? SuspensionTravel * 0.5f - LastCompression[i] : 0.f;
		const FVector Pos = W.Position - BoxCenter - FVector(0.f, 0.f, FMath::Clamp(Drop, -SuspensionTravel * 0.5f, SuspensionTravel * 0.5f));
		WheelMeshes[i]->SetRelativeLocation(Pos);
		const float Yaw = W.bSteer ? Steer * SteerLock : 0.f;
		WheelMeshes[i]->SetRelativeRotation(FRotator(-FMath::RadiansToDegrees(WheelSpin[i]), Yaw, 0.f));
	}
}

// --- seats ------------------------------------------------------------------------------------

float ADFVehicle::BotDesirability(const ADFCharacter* Bot) const {
	return bBotsDrive ? SeatDesirability(Bot) : -1.f;
}

float ADFVehicle::SeatDesirability(const ADFCharacter* Bot) const {
	// Vehicle.BotDesireability
	if (!Bot || Health <= 0 || bBlownUp || IsActorBeingDestroyed() || bLocked || !bDriveable || IsUpsideDown() || GetWorld()->GetTimeSeconds() < BotStuckUntil || GetVelocity().Size() >= KillSpeed)
		return -1.f;
	float Desire = 1.1f - 0.3f * FMath::CountBits(BlownWheels);
	int32 Taken = 0;
	for (int32 Seat = 0; Seat < SeatPositions.Num(); ++Seat) {
		const ADFCharacter* In = Seat == 0 ? Driver.Get() : (Passengers.IsValidIndex(Seat) ? Passengers[Seat].Get() : nullptr);
		if (In) {
			++Taken;
			Desire += (In->GetTeam() == Bot->GetTeam() ? 1.f : -1.f) * (Seat == 0 ? 0.5f : 0.2f);
		}
	}
	return Taken >= SeatPositions.Num() ? -1.f : Desire;
}

bool ADFVehicle::IsNear(const ADFCharacter* C) const {
	if (!C)
		return false;
	for (int32 D = 0; D < DoorPositions.Num(); ++D)
		if (FVector::Dist(DoorExit(D), C->GetActorLocation()) < 250.f)
			return true;
	return FVector::Dist(GetActorLocation(), C->GetActorLocation()) < BoxExtent.Size() + 120.f;
}

FVector ADFVehicle::DoorExit(int32 Door) const {
	const FVector Local = DoorPositions.IsValidIndex(Door) ? DoorPositions[Door] : FVector(0.f, BoxExtent.Y + 80.f, 0.f);
	return GetActorTransform().TransformPosition(Local - BoxCenter) + FVector(0.f, 0.f, 60.f);
}

int32 ADFVehicle::GetSeatOf(const ADFCharacter* C) const {
	if (C && C == Driver)
		return 0;
	const int32 I = C ? Passengers.IndexOfByKey(C) : INDEX_NONE;
	return I > 0 ? I : INDEX_NONE;
}

FVector ADFVehicle::GetSeatOffset(int32 Seat, const ADFCharacter* C) const {
	const FVector Eye = SeatPositions.IsValidIndex(Seat) ? SeatPositions[Seat] : FVector::ZeroVector;
	return Eye - BoxCenter - FVector(0.f, 0.f, C ? C->BaseEyeHeight : 0.f);
}

void ADFVehicle::SeatPassenger(ADFCharacter* C, int32 Seat) {
	Passengers.SetNum(FMath::Max(Passengers.Num(), SeatPositions.Num()));
	Passengers[Seat] = C;
	// rides along at the seat, still a soldier: looks around and shoots (InCarState), never
	// pushes the vehicle it sits in
	C->GetCharacterMovement()->DisableMovement();
	C->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	C->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
	C->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Body->IgnoreActorWhenMoving(C, true);
	C->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	C->SetActorRelativeLocation(GetSeatOffset(Seat, C));
	PassengerEntered(C, Seat);
}

void ADFVehicle::UnseatPassenger(ADFCharacter* C) {
	const int32 Seat = Passengers.IndexOfByKey(C);
	if (Seat == INDEX_NONE)
		return;
	PassengerLeft(C, Seat);
	Passengers[Seat] = nullptr;
	Body->IgnoreActorWhenMoving(C, false);
	C->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	const UCapsuleComponent* Def = GetDefault<ACharacter>(C->GetClass())->GetCapsuleComponent();
	for (const ECollisionChannel Ch : { ECC_PhysicsBody, ECC_Vehicle, ECC_Pawn })
		C->GetCapsuleComponent()->SetCollisionResponseToChannel(Ch, Def ? Def->GetCollisionResponseToChannel(Ch) : ECR_Block);
}

bool ADFVehicle::FindExitSpot(const ADFCharacter* C, FVector& OutSpot) const {
	// first door with room for a soldier, then the roof (a car on its side, doors against
	// walls: getting out over the top beats being stuck inside)
	const FVector Roof = GetActorLocation() + FVector(0.f, 0.f, BoxExtent.Z + C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20.f);
	OutSpot = Roof;
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(C->GetCapsuleComponent()->GetScaledCapsuleRadius(), C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFVehicleExit), false, this);
	Params.AddIgnoredActor(C);
	for (int32 D = 0; D <= FMath::Max(1, DoorPositions.Num()); ++D) {
		const FVector Try = D < FMath::Max(1, DoorPositions.Num()) ? DoorExit(D) : Roof;
		if (!GetWorld()->OverlapAnyTestByChannel(Try, FQuat::Identity, ECC_Pawn, Shape, Params)) {
			OutSpot = Try;
			return true;
		}
	}
	return false;
}

bool ADFVehicle::ExitPassenger(ADFCharacter* C, bool bForce) {
	if (!HasAuthority() || !C || Passengers.IndexOfByKey(C) <= 0)
		return false;
	FVector Spot;
	if (!FindExitSpot(C, Spot) && !bForce)
		return false; // boxed in: stay inside
	UnseatPassenger(C);
	C->TeleportTo(Spot, FRotator(0.f, GetActorRotation().Yaw, 0.f));
	C->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	C->GetCharacterMovement()->Velocity = GetVelocity() * 0.5f;
	return true;
}

void ADFVehicle::PassengerDied(ADFCharacter* C) {
	if (!HasAuthority() || !C)
		return;
	if (C == Driver)
		Exit(true);
	else if (Passengers.Contains(C))
		UnseatPassenger(C);
}

bool ADFVehicle::EnterSeat(ADFCharacter* C, int32 Seat) {
	if (Seat <= 0)
		return TryEnter(C);
	if (!HasAuthority() || bBlownUp || bLocked || !C || !C->IsAlive() || !IsNear(C) || GetSeatOf(C) != INDEX_NONE || !SeatPositions.IsValidIndex(Seat))
		return false;
	Passengers.SetNum(FMath::Max(Passengers.Num(), SeatPositions.Num()));
	if (Passengers[Seat])
		return false;
	SeatPassenger(C, Seat);
	return true;
}

bool ADFVehicle::TryEnter(ADFCharacter* C) {
	if (!HasAuthority() || bBlownUp || bLocked || !C || !C->IsAlive() || !IsNear(C) || C->IsInLoadout() || GetSeatOf(C) != INDEX_NONE)
		return false;
	// MF PawnEnterVehicle: the nearest door with a free seat, the seats behind it in MF's order
	// (the buggy's back door is the gunner's, the humvee's the back seats'); then any free one
	TArray<int32> Order;
	if (DoorSeats.Num() && DoorPositions.Num()) {
		TArray<int32> Doors;
		for (int32 D = 0; D < DoorPositions.Num(); ++D)
			Doors.Add(D);
		const FVector At = C->GetActorLocation();
		Doors.Sort([this, &At](int32 A, int32 B) { return FVector::DistSquared(DoorExit(A), At) < FVector::DistSquared(DoorExit(B), At); });
		for (const int32 D : Doors) {
			if (DoorSeats.IsValidIndex(D))
				for (const int32 Seat : DoorSeats[D].Seats)
					Order.AddUnique(Seat);
		}
	}
	for (int32 Seat = 0; Seat < FMath::Max(1, SeatPositions.Num()); ++Seat)
		Order.AddUnique(Seat);
	Passengers.SetNum(FMath::Max(Passengers.Num(), SeatPositions.Num()));
	for (const int32 Seat : Order) {
		if (Seat == 0 && bDriveable && !Driver)
			return EnterDriver(C);
		if (Seat > 0 && Passengers.IsValidIndex(Seat) && !Passengers[Seat]) {
			SeatPassenger(C, Seat);
			return true;
		}
	}
	return false;
}

bool ADFVehicle::EnterDriver(ADFCharacter* C) {
	AController* PC = C->GetController();
	if (!PC)
		return false;
	Driver = C;
	if (ADFWeapon* W = C->GetCurrentWeapon())
		W->ForceInactive();
	C->SetActorEnableCollision(false);
	C->GetCharacterMovement()->DisableMovement(); // stays in sight at the wheel (Drive animations)
	C->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	C->SetActorRelativeLocation(GetSeatOffset(0, C));
	LastDriver = C;
	if (ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>())
		GM->VehicleEntered(this, C); // before possession: the soldier still has the player state
	const FRotator View = PC->GetControlRotation();
	PC->Possess(this);
	PC->SetControlRotation(View);
	Body->WakeAllRigidBodies();
	return true;
}

void ADFVehicle::ExitAll() {
	if (!HasAuthority())
		return;
	Exit(true);
	for (ADFCharacter* P : TArray<TObjectPtr<ADFCharacter>>(Passengers))
		if (P)
			ExitPassenger(P, true);
}

void ADFVehicle::Exit(bool bForce) {
	ADFCharacter* C = Driver;
	if (!HasAuthority() || !C)
		return;
	FVector Spot;
	if (!FindExitSpot(C, Spot) && !bForce)
		return; // boxed in: stay inside
	AController* PC = GetController();
	Driver = nullptr;
	bDriverFire = bDriverAltFire = bNitro = false;
	C->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	C->SetActorHiddenInGame(false);
	C->SetActorEnableCollision(true);
	C->TeleportTo(Spot, FRotator(0.f, GetActorRotation().Yaw, 0.f));
	C->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	C->GetCharacterMovement()->Velocity = GetVelocity() * 0.5f;
	if (PC) {
		const FRotator View = PC->GetControlRotation();
		PC->Possess(C);
		PC->SetControlRotation(View);
	}
}

void ADFVehicle::Destroyed() {
	UE_LOG(LogDF, Verbose, TEXT("Vehicle %s gone at %s"), *GetName(), *GetActorLocation().ToCompactString());
	if (HasAuthority()) {
		ExitAll();
		if (Trailer)
			Trailer->Destroy(); // MF Vehicle.SilentDestroy: the trailer goes with the cab
		if (Generator.IsValid())
			Generator->VehicleGone();
	}
	Super::Destroyed();
}

void ADFVehicle::HitchTrailer(ADFVehicle* Other, const FVector& HitchPoint) {
	if (!HasAuthority() || !Other || Other == this)
		return;
	Trailer = Other;
	// ball joint: free to swing behind the cab, some pitch over bumps, little roll
	Hitch = NewObject<UPhysicsConstraintComponent>(this);
	Hitch->SetupAttachment(Body);
	Hitch->RegisterComponent();
	Hitch->SetRelativeLocation(HitchPoint - BoxCenter);
	Hitch->SetDisableCollision(true);
	Hitch->SetLinearXLimit(LCM_Locked, 0.f);
	Hitch->SetLinearYLimit(LCM_Locked, 0.f);
	Hitch->SetLinearZLimit(LCM_Locked, 0.f);
	Hitch->SetAngularSwing1Limit(ACM_Limited, 75.f);
	Hitch->SetAngularSwing2Limit(ACM_Limited, 25.f);
	Hitch->SetAngularTwistLimit(ACM_Limited, 15.f);
	Hitch->SetConstrainedComponents(Body, NAME_None, Other->Body, NAME_None);
}

void ADFVehicle::LockDown() {
	bLocked = true;
	ExitAll();
	if (Trailer)
		Trailer->LockDown();
}

// --- damage --------------------------------------------------------------------------------------

float ADFVehicle::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) {
	if (!HasAuthority() || bBlownUp || Damage <= 0.f)
		return 0.f;
	const UDFDamageType* Type = DamageEvent.DamageTypeClass ? Cast<UDFDamageType>(DamageEvent.DamageTypeClass->GetDefaultObject()) : nullptr;
	const FTransform T = Body->GetComponentTransform();
	FVector At = GetActorLocation();
	bool bTyre = false;
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID)) {
		const FPointDamageEvent& Point = static_cast<const FPointDamageEvent&>(DamageEvent);
		At = Point.HitInfo.ImpactPoint;
		UE_LOG(LogDF, Verbose, TEXT("%s hit: %s at %s (%.0f)"), *GetName(), *GetNameSafe(Point.HitInfo.GetComponent()),
			*GetActorTransform().InverseTransformPosition(At).ToCompactString(), Damage);
		// MF Collidables: a tyre (WheelCollision, 35 health, any damage) ...
		const int32 Wheel = WheelColliders.IndexOfByKey(Point.HitInfo.GetComponent());
		if (Wheel != INDEX_NONE) {
			bTyre = true;
			WheelDamage[Wheel] += FMath::RoundToInt(Damage);
			if (WheelDamage[Wheel] >= WheelHealth)
				BlowOutWheel(Wheel);
		}
		// ... or the petrol cap (PetrolCap: 150, bullets and pellets only): its sphere was hit
		// through a gap, or the shot hit the skin right over it
		else if (PetrolCapHealth > 0 && Type && Type->bHitsPetrolCap) {
			const FVector Cap = T.TransformPosition(PetrolCapPosition - BoxCenter);
			const FVector Dir = Point.ShotDirection.GetSafeNormal();
			const float Along = FVector::DotProduct(Cap - At, Dir);
			const bool bOverCap = Along > -PetrolCapRadius && Along < PetrolCapRadius + 20.f && FVector::DistSquared(At + Dir * Along, Cap) <= FMath::Square(PetrolCapRadius);
			if (Point.HitInfo.GetComponent() == PetrolCapCollider || bOverCap) {
				UE_LOG(LogDF, Log, TEXT("%s: petrol cap hit, %d left"), *GetName(), PetrolCapHealth - FMath::RoundToInt(Damage));
				PetrolCapHealth -= FMath::RoundToInt(Damage);
				if (PetrolCapHealth < 0) {
					MulticastHitFX(At, false);
					BlowUp(EventInstigator);
					return Damage;
				}
			}
		}
	} else if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID)) {
		// HurtRadius hits the tyres too (they are actors in MF)
		const FRadialDamageEvent& Radial = static_cast<const FRadialDamageEvent&>(DamageEvent);
		const float Radius = FMath::Max(Radial.Params.OuterRadius, 1.f);
		for (int32 i = 0; i < WheelMeshes.Num(); ++i) {
			const float Dist = FVector::Dist(WheelMeshes[i]->GetComponentLocation(), Radial.Origin);
			WheelDamage[i] += FMath::RoundToInt(Radial.Params.BaseDamage * FMath::Max(0.f, 1.f - Dist / Radius));
			if (WheelDamage[i] >= WheelHealth)
				BlowOutWheel(i);
		}
	}
	MulticastHitFX(At, bTyre);
	ApplyHitPush(DamageEvent, DamageCauser);
	if (bTyre)
		return Damage; // the tyre took it, not the car
	const int32 Applied = ScaleDamage(Damage, Type, EventInstigator);
	if (Applied <= 0)
		return 0.f;
	Health -= Applied;
	if (Health <= 0)
		BlowUp(EventInstigator);
	return float(Applied);
}

int32 ADFVehicle::ScaleDamage(float Damage, const UDFDamageType* Type, AController* By) const {
	// the damage type says how much reaches the hull (UDFDamageType::VehicleDamageScale: MF
	// Vehicle.TakeDamage x 1.5 for explosives, the servers' changeDamage for the rest), then
	// Physical's armour; bulletproof hulls ignore gunpowder; bots do a fifth (changeDamage)
	if (!Type || (Type->bBallistic && bBulletproof))
		return 0;
	const ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>();
	float D = Damage * Type->VehicleDamageScale * (1.f - Armour);
	if (GM && By && !By->IsPlayerController())
		D *= GM->BotVehicleDamageScale;
	return D > 0.f ? FMath::Max(1, FMath::RoundToInt(D)) : 0;
}

void ADFVehicle::ApplyHitPush(const FDamageEvent& DamageEvent, AActor* DamageCauser) {
	// Physical.TakeDamage: ApplyForce(Normal(Momentum) * Sqrt(Min(|Momentum|, 100000) * 0.1)),
	// pellets only 40 % of the time. MF momentum in uu; the push is a jolt sized for an 800 kg car
	float Momentum = 0.f;
	if (const ADFWeapon* W = Cast<ADFWeapon>(DamageCauser))
		Momentum = W->HitMomentum / 2.25f;
	else if (const ADFProjectile* P = Cast<ADFProjectile>(DamageCauser))
		Momentum = P->Momentum / 2.25f;
	FVector Dir = FVector::ZeroVector, At = GetActorLocation();
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID)) {
		const FPointDamageEvent& Point = static_cast<const FPointDamageEvent&>(DamageEvent);
		Dir = Point.ShotDirection.GetSafeNormal();
		At = Point.HitInfo.ImpactPoint;
		if (Point.DamageTypeClass && Point.DamageTypeClass->IsChildOf<UDFDamage_Shotgun>() && FMath::FRand() >= 0.4f)
			return;
	} else if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID)) {
		const FRadialDamageEvent& Radial = static_cast<const FRadialDamageEvent&>(DamageEvent);
		Dir = (GetActorLocation() - Radial.Origin).GetSafeNormal();
		Momentum *= FMath::Max(0.f, 1.f - FVector::Dist(GetActorLocation(), Radial.Origin) / FMath::Max(Radial.Params.OuterRadius, 1.f));
	}
	if (Momentum > 0.f && !Dir.IsNearlyZero() && Body->IsSimulatingPhysics())
		Body->AddImpulseAtLocation(Dir * FMath::Sqrt(FMath::Min(Momentum, 100000.f) * 0.1f) * 2.25f * 800.f, At);
}

void ADFVehicle::BlowOutWheel(int32 Index) {
	if (!HasAuthority() || !WheelMeshes.IsValidIndex(Index) || IsWheelBlown(Index))
		return;
	// MF DestroyWheel: smoke, a jolt up at the wheel, flat for ReInflatePause
	UE_LOG(LogDF, Log, TEXT("%s: tyre %d burst"), *GetName(), Index);
	BlownWheels |= uint8(1 << Index);
	WheelDamage[Index] = 0;
	OnRep_BlownWheels();
	if (Body->IsSimulatingPhysics())
		Body->AddImpulseAtLocation(GetActorUpVector() * 50.f * 2.25f * 800.f, WheelMeshes[Index]->GetComponentLocation());
	GetWorldTimerManager().SetTimer(ReInflateTimers[Index], FTimerDelegate::CreateWeakLambda(this, [this, Index]() {
		BlownWheels &= uint8(~(1 << Index));
		OnRep_BlownWheels();
	}),
		ReInflatePause, false);
}

void ADFVehicle::OnRep_BlownWheels() {
	for (int32 i = 0; i < WheelMeshes.Num() && i < Wheels.Num(); ++i) {
		const FString& Want = IsWheelBlown(i) && !Wheels[i].BlownMesh.IsEmpty() ? Wheels[i].BlownMesh : Wheels[i].Mesh;
		const FVector Loc = WheelMeshes[i]->GetRelativeLocation();
		const FRotator Rot = WheelMeshes[i]->GetRelativeRotation();
		if (DF::ApplyPropMesh(WheelMeshes[i], *Want, 1.f, Loc))
			WheelMeshes[i]->SetRelativeRotation(Rot);
	}
	if (BlownWheels && GetNetMode() != NM_DedicatedServer)
		UGameplayStatics::PlaySoundAtLocation(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/MiscSFX/HitRubber")), GetActorLocation());
	// DestroyWheel: EffectsFix.SmokePuffVeh at the wheel that just went
	for (int32 i = 0; i < WheelMeshes.Num(); ++i)
		if ((BlownWheels & ~ShownBlownWheels) & (1 << i))
			DF::SpawnEffect(this, TEXT("SmokePuffVeh"), WheelMeshes[i]->GetComponentLocation());
	ShownBlownWheels = BlownWheels;
}

void ADFVehicle::MulticastHitFX_Implementation(FVector_NetQuantize At, bool bTyre) {
	if (GetNetMode() != NM_DedicatedServer)
		UGameplayStatics::PlaySoundAtLocation(this, DF::LoadClassic<USoundBase>(bTyre ? TEXT("Sounds/MiscSFX/HitRubber") : TEXT("Sounds/MiscSFX/HitMetal")), At);
}

void ADFVehicle::BlowUp(AController* By) {
	bBlownUp = true;
	if (Trailer && !Trailer->bBlownUp)
		Trailer->BlowUp(By);
	TArray<ADFCharacter*> Inside;
	if (Driver)
		Inside.Add(Driver);
	for (ADFCharacter* P : Passengers)
		if (P)
			Inside.Add(P);
	ExitAll();
	for (ADFCharacter* C : Inside) {
		FDamageEvent Ev(UDFDamage_Explosion::StaticClass());
		C->TakeDamage(1000.f, Ev, By, this);
	}
	TArray<AActor*> Ignore{ this };
	UGameplayStatics::ApplyRadialDamageWithFalloff(this, 150.f, 0.f, GetActorLocation(), 0.f, 900.f, 1.f,
		UDFDamage_Explosion::StaticClass(), Ignore, this, By, ECC_Visibility);
	MulticastBlowUpFX();
	SetLifeSpan(0.1f);
}

void ADFVehicle::MulticastBlowUpFX_Implementation() {
	if (GetNetMode() != NM_DedicatedServer) {
		UGameplayStatics::PlaySoundAtLocation(this, DF::LoadSound(TEXT("MiscSFX.Explode")), GetActorLocation());
		AVehicleFrag::Burst(GetWorld(), GetActorLocation());
		AVehicleFrag::Explosion(GetWorld(), GetActorLocation()); // EffectsFix.VehExplosion
	}
}

void ADFVehicle::UpdateEngineSound() {
	// Car state Driving: EngineStartSound, 0.5 s later the engine loop at SoundPitch 64 + 2 x
	// speed / 60 (uu/s; 64 = normal pitch), EndState: EngineStopSound
	const bool bWant = bDriveable && Driver && !bBlownUp && !EngineLoopSound.IsEmpty();
	if (bWant != bEngineOn) {
		bEngineOn = bWant;
		if (bWant) {
			UGameplayStatics::SpawnSoundAttached(DF::LoadClassic<USoundBase>(EngineStartSound), Body,
				NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, true, 0.8f, 1.f, 0.f, DF::GetAttenuation(1600.f));
			GetWorldTimerManager().SetTimer(EngineStartTimer, FTimerDelegate::CreateWeakLambda(this, [this]() {
				if (!bEngineOn)
					return;
				if (!EngineAudio)
					EngineAudio = UGameplayStatics::SpawnSoundAttached(DF::LoadClassic<USoundBase>(EngineLoopSound), Body,
						NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, true, 0.5f, 1.f, 0.f, DF::GetAttenuation(2500.f), nullptr, false);
				else
					EngineAudio->Play();
			}),
				0.5f, false);
		} else {
			GetWorldTimerManager().ClearTimer(EngineStartTimer);
			if (EngineAudio)
				EngineAudio->Stop();
			if (!bBlownUp)
				DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(EngineStopSound), GetActorLocation(), 0.8f);
		}
	}
	if (EngineAudio && EngineAudio->IsPlaying())
		EngineAudio->SetPitchMultiplier(GetEnginePitch());
	// no skid sound: MF's Vehicle.Driving had its PlaySound(SkidSound) commented out
}

void ADFVehicle::PassengerEntered(ADFCharacter* C, int32 Seat) {
	TInlineComponentArray<UDFVehicleTurret*> Turrets(this);
	for (UDFVehicleTurret* T : Turrets)
		T->SeatEntered(C, Seat);
}

void ADFVehicle::PassengerLeft(ADFCharacter* C, int32 Seat) {
	TInlineComponentArray<UDFVehicleTurret*> Turrets(this);
	for (UDFVehicleTurret* T : Turrets)
		T->SeatLeft(C, Seat);
}

void ADFVehicle::MulticastWheelThump_Implementation(float Volume) {
	DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/WheelSFX/HitWheel")), GetActorLocation(), Volume, 1600.f, 0.75f + FMath::FRand() * 0.25f);
}

void ADFVehicle::MulticastBump_Implementation(float Volume) {
	static const TCHAR* Bumps[] = { TEXT("Sounds/MiscSFX/bump1"), TEXT("Sounds/MiscSFX/bump2"), TEXT("Sounds/MiscSFX/bump3"), TEXT("Sounds/MiscSFX/bump4") };
	DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(Bumps[FMath::RandRange(0, 3)]), GetActorLocation(), Volume);
}

void ADFVehicle::OnBodyHit(UPrimitiveComponent* HitComp, AActor* Other, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit) {
	// Vehicle.HitWall: a bump over 100 uu/s (louder up to 700), one at a time
	if (HasAuthority() && !Cast<APawn>(Other) && GetWorld()->GetTimeSeconds() >= BumpSoundUntil) {
		const float HitSpeed = FMath::Abs(FVector::DotProduct(GetVelocity(), Hit.ImpactNormal)) / 2.25f;
		if (HitSpeed > 100.f) {
			UE_LOG(LogDF, Verbose, TEXT("%s bumped %s (%s) at %s, %.0f uu/s"), *GetName(), *GetNameSafe(Other), *GetNameSafe(OtherComp), *Hit.ImpactPoint.ToCompactString(), HitSpeed);
			BumpSoundUntil = GetWorld()->GetTimeSeconds() + 0.5f;
			MulticastBump(FMath::Min(HitSpeed, 700.f) / 700.f * 0.8f);
		}
	}
	// MF Vehicle.CheckKill (Bump): the car's speed at the soldier, KillSpeed or more, runs him
	// down (100 'RunDown'). Judged on the speed before this frame's contact: by the time the
	// hit is reported the physics has already stopped the car against the soldier's capsule
	ADFCharacter* C = Cast<ADFCharacter>(Other);
	if (!HasAuthority() || !C || !C->IsAlive() || C == Driver || GetSeatOf(C) != INDEX_NONE || C->GetMovementBaseObject() == Body || FMath::Abs(Hit.ImpactNormal.Z) > 0.7f)
		return; // riders, and soldiers standing on it, aren't run over
	const FVector Away = (C->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	const float Closing = FVector::DotProduct(PrevVelocity, Away); // the car's, not the soldier walking into it
	if (Closing < 50.f)
		return;
	if (PrevVelocity.Size() >= KillSpeed) {
		FDamageEvent Ev(UDFDamage_RunOver::StaticClass());
		C->TakeDamage(100.f, Ev, GetController(), this);
	}
	// a soldier doesn't stop a car: he is shoved aside, the car keeps nearly all its way
	// (80 kg against its mass)
	if (C->IsAlive())
		C->LaunchCharacter(Away * Closing * 1.1f + FVector(0.f, 0.f, FMath::Min(Closing * 0.3f, 400.f)), true, true);
	Body->SetPhysicsLinearVelocity(PrevVelocity * (MassKg / (MassKg + 80.f)));
}

// --- generator -------------------------------------------------------------------------------------

AVehicleGenerator::AVehicleGenerator() {
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AVehicleGenerator::BeginPlay() {
	Super::BeginPlay();
	if (HasAuthority())
		Generate();
}

void AVehicleGenerator::Generate() {
	if (Spawned.IsValid() || !VehicleClass)
		return;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
	// VehicleGenerator.CreateVehicle: 100 uu above the generator
	ADFVehicle* V = GetWorld()->SpawnActor<ADFVehicle>(VehicleClass, GetActorLocation() + FVector(0.f, 0.f, 225.f), GetActorRotation(), SP);
	if (V) {
		V->Generator = this;
		Spawned = V;
		UE_LOG(LogDF, Verbose, TEXT("Vehicle %s at %s"), *V->GetName(), *V->GetActorLocation().ToCompactString());
	} else {
		UE_LOG(LogDF, Verbose, TEXT("%s: no room for a %s"), *GetName(), *GetNameSafe(VehicleClass));
		GetWorldTimerManager().SetTimer(Timer, this, &AVehicleGenerator::Generate, 5.f, false); // something parked on it
	}
}

void AVehicleGenerator::VehicleGone() {
	Spawned = nullptr;
	GetWorldTimerManager().SetTimer(Timer, this, &AVehicleGenerator::Generate, 5.f, false);
}

UClass* AVehicleGenerator::ClassForMF(const FString& MFClass) {
	// MF vehicle classes as maps and server configs name them ("heli.heli", "Rage.Truck"); the
	// plane is looked up by name (Vehicles/Piper)
	static const TPair<const TCHAR*, UClass*> ByClass[] = {
		{ TEXT("boat.boat"), ABoat::StaticClass() },
		{ TEXT("jeep.jeep"), AJeep::StaticClass() },
		{ TEXT("heli.heli"), AHeli::StaticClass() },
		{ TEXT("heli.heliHmg"), AHeliHmg::StaticClass() },
		{ TEXT("Rage.Buggy"), ABuggy::StaticClass() },
		{ TEXT("Rage.Humvee"), AHumvee::StaticClass() },
		{ TEXT("Rage.Truck"), ATruck::StaticClass() },
		{ TEXT("Rage.Armadillo"), AArmadillo::StaticClass() },
	};
	for (const auto& Entry : ByClass)
		if (MFClass.Equals(Entry.Key, ESearchCase::IgnoreCase))
			return Entry.Value;
	if (MFClass.StartsWith(TEXT("piper."), ESearchCase::IgnoreCase))
		return LoadClass<ADFVehicle>(nullptr, MFClass.EndsWith(TEXT("Ts"), ESearchCase::IgnoreCase) ? TEXT("/Script/DesktopForces.PiperTs") : TEXT("/Script/DesktopForces.Piper"));
	return nullptr;
}

void AVehicleGenerator::SpawnAll(UWorld* World, const TArray<FVector>& Exclude) {
	static const TPair<const TCHAR*, UClass*> Map[] = {
		{ TEXT("BuggyGenerator"), ABuggy::StaticClass() },
		{ TEXT("HumveeGenerator"), AHumvee::StaticClass() },
		{ TEXT("TruckGenerator"), ATruck::StaticClass() },
		{ TEXT("ArmadilloGenerator"), AArmadillo::StaticClass() },
	};
	TArray<TPair<ADFLegacyActor*, UClass*>> Spots;
	for (const auto& Entry : Map)
		for (ADFLegacyActor* L : ADFLegacyActor::FindAll(World, Entry.Key))
			Spots.Add({ L, Entry.Value });
	// MF VehicleGenerator with VehicleClass=Class'boat.boat' (MF-mods vehicles on community maps)
	for (ADFLegacyActor* L : ADFLegacyActor::FindAll(World, TEXT("VehicleGenerator"))) {
		FString Class = L->Properties.FindRef(TEXT("VehicleClass"));
		Class.Split(TEXT("'"), nullptr, &Class);
		Class.Split(TEXT("'"), &Class, nullptr);
		if (UClass* C = ClassForMF(Class))
			Spots.Add({ L, C });
	}
	int32 N = 0;
	// mutPack addVehicles: the extra vehicles the settings list for this map
	const ADFGameMode* AddGM = World->GetAuthGameMode<ADFGameMode>();
	if (!AddGM || AddGM->bAddVehicles) {
		const FString MapName = UWorld::RemovePIEPrefix(World->GetMapName());
		for (const FDFAddedVehicle& Add : UDFGameSettings::Get().AddedVehicles) {
			UClass* Class = ClassForMF(Add.VehicleClass);
			if (!Class || !MapName.Equals(Add.Map, ESearchCase::IgnoreCase))
				continue;
			const FTransform T(FRotator(0.f, Add.Yaw, 0.f), Add.Location);
			if (AVehicleGenerator* G = World->SpawnActorDeferred<AVehicleGenerator>(AVehicleGenerator::StaticClass(), T)) {
				G->VehicleClass = Class;
				G->FinishSpawning(T);
				++N;
			}
		}
	}
	for (const auto& Spot : Spots) {
		const TPair<const TCHAR*, UClass*> Entry(nullptr, Spot.Value);
		for (ADFLegacyActor* L : { Spot.Key }) {
			const FString* Auto = L->Properties.Find(TEXT("bAutoGenerate"));
			if (Auto && Auto->Equals(TEXT("False"), ESearchCase::IgnoreCase))
				continue;
			if (Exclude.ContainsByPredicate([L](const FVector& P) { return FVector::Dist(P, L->GetActorLocation()) < 800.f; }))
				continue;
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AVehicleGenerator* G = World->SpawnActorDeferred<AVehicleGenerator>(AVehicleGenerator::StaticClass(), L->GetActorTransform())) {
				const ADFGameMode* GM = World->GetAuthGameMode<ADFGameMode>();
				G->VehicleClass = GM ? GM->GetVehicleClassFor(Entry.Value) : Entry.Value;
				G->FinishSpawning(L->GetActorTransform());
				++N;
			}
		}
	}
	UE_LOG(LogDF, Log, TEXT("Vehicles: %d generators"), N);
}

FDFWheelSetup DF::RageWheel(const FVector& Pos, float Radius, bool bSteer, bool bDrive, const TCHAR* Mesh) {
	FDFWheelSetup W;
	W.Position = Pos;
	W.Radius = Radius;
	W.bSteer = bSteer;
	W.bDrive = bDrive;
	W.Mesh = Mesh;
	// MF WheelBlownMesh: BuggyWheelFR -> BuggyWheelFRD, humwheelL -> HumveeWheelDL,
	// TruckWheelL -> TruckWheelDL (Armadillo, Trailer the same)
	const FString M(Mesh);
	if (M.Contains(TEXT("Buggy")))
		W.BlownMesh = M + TEXT("D");
	else if (M.Contains(TEXT("humwheel")))
		W.BlownMesh = TEXT("Rage/HumveeWheelD") + M.Right(1);
	else
		W.BlownMesh = M.LeftChop(1) + TEXT("D") + M.Right(1);
	return W;
}

#undef LOCTEXT_NAMESPACE

void ADFVehicle::UpdateFlag() {
	const ADFGameState* GS = GetWorld()->GetGameState<ADFGameState>();
	const uint8 Team = Driver && GS && GS->IsTeamGame() ? Driver->GetTeam() : DF::NoTeam;
	if (Team == FlagTeam)
		return;
	FlagTeam = Team;
	if (!FlagPole) {
		// a pole and a pennant (MF's waving VehicleFlag is a skeletal mesh not converted yet)
		UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		auto Make = [this](UStaticMesh* Mesh) {
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
			C->SetStaticMesh(Mesh);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCastShadow(false);
			C->SetCanEverAffectNavigation(false);
			C->SetupAttachment(Body);
			C->RegisterComponent();
			return C;
		};
		FlagPole = Make(Cylinder);
		FlagPole->SetRelativeLocation(FlagPosition - BoxCenter + FVector(0.f, 0.f, 60.f));
		FlagPole->SetRelativeScale3D(FVector(0.025f, 0.025f, 1.2f)); // 2.5 cm thick, 120 cm tall
		FlagCloth = Make(Plane);
		FlagCloth->SetRelativeLocation(FlagPosition - BoxCenter + FVector(-25.f, 0.f, 100.f));
		FlagCloth->SetRelativeRotation(FRotator(0.f, 0.f, 90.f)); // upright, along the car
		FlagCloth->SetRelativeScale3D(FVector(0.5f, 0.32f, 1.f)); // 50 x 32 cm
		if (UMaterialInterface* M = UDFGameSettings::Get().GetTeamTintMaterial())
			FlagCloth->SetMaterial(0, M);
	}
	const bool bShow = Team != DF::NoTeam;
	FlagPole->SetVisibility(bShow);
	FlagCloth->SetVisibility(bShow);
	if (bShow) {
		const FLinearColor C = UDFGameSettings::Get().GetTeamColor(Team);
		FlagCloth->SetCustomPrimitiveDataVector4(0, FVector4(C.R, C.G, C.B, 1.f));
	}
}

// --- MF engine and gearbox ------------------------------------------------------------------

TArray<FDFDoorSeats> DF::DoorSeats(std::initializer_list<std::initializer_list<int32>> PerDoor) {
	TArray<FDFDoorSeats> Out;
	for (const std::initializer_list<int32>& Door : PerDoor)
		Out.AddDefaulted_GetRef().Seats = TArray<int32>(Door);
	return Out;
}

void ADFVehicle::SetHandling(std::initializer_list<float> Torque, std::initializer_list<float> Rpm, std::initializer_list<float> Ratios, float Reverse, float Final) {
	TorqueCurve = TArray<float>(Torque);
	TorqueRpm = TArray<float>(Rpm);
	GearRatios.Reset();
	for (const float R : Ratios)
		if (R > 0.f)
			GearRatios.Add(R); // the file pads the gears with 0.00
	ReverseRatio = Reverse;
	FinalDrive = Final;
}

float ADFVehicle::GetDriveForce(float ForwardSpeed, float InThrottle) const {
	if (FMath::IsNearlyZero(InThrottle) || TorqueCurve.Num() < 2 || TorqueCurve.Num() != TorqueRpm.Num())
		return 0.f;
	// driven wheels' radius (cm)
	float Radius = 0.f;
	int32 N = 0;
	for (const FDFWheelSetup& W : Wheels) {
		if (W.bDrive) {
			Radius += W.Radius;
			++N;
		}
	}
	Radius = N ? Radius / N : 40.f;
	auto TorqueAt = [this](float Rpm) {
		// the clutch slips below the curve's first point; past its end the engine gives nothing
		Rpm = FMath::Max(Rpm, TorqueRpm[0]);
		for (int32 i = 1; i < TorqueRpm.Num(); ++i) {
			if (Rpm <= TorqueRpm[i]) {
				const float A = (Rpm - TorqueRpm[i - 1]) / FMath::Max(TorqueRpm[i] - TorqueRpm[i - 1], 1.f);
				return FMath::Lerp(TorqueCurve[i - 1], TorqueCurve[i], A);
			}
		}
		return 0.f;
	};
	const bool bReverse = InThrottle < 0.f;
	const float WheelRpm = FMath::Abs(ForwardSpeed) / (2.f * PI * Radius) * 60.f;
	float Pull = 0.f; // N
	auto Gear = [&](float Ratio) { Pull = FMath::Max(Pull, TorqueAt(WheelRpm * Ratio * FinalDrive) * Ratio * FinalDrive / (Radius / 100.f)); };
	if (bReverse)
		Gear(ReverseRatio);
	else
		for (const float R : GearRatios)
			Gear(R); // an automatic box: whichever gear pulls hardest at this speed
	// MaxSpeed is a governor: the pull fades over its last 3 %
	const float Top = TopSpeedKmh / 0.036f;
	if (!bReverse)
		Pull *= FMath::Clamp((Top - ForwardSpeed) / (Top * 0.03f), 0.f, 1.f);
	const float Force = FMath::Min(Pull * 100.f, MassKg * MaxDriveAccel); // N -> kg cm/s^2
	return Force * InThrottle;
}
