#include "Vehicles/MFVehicle.h"
#include "Vehicles/MFVehicles.h"
#include "MobileForces.h"
#include "Core/MFProps.h"
#include "Player/MFCharacter.h"
#include "Weapons/MFWeapon.h"
#include "World/MFMapActors.h"
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
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MFVehicle"

UMFDamage_RunOver::UMFDamage_RunOver()
{
	DeathVerb = LOCTEXT("RanOver", "ran over");
}

AMFVehicle::AMFVehicle()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	SetReplicatingMovement(true);
	AutoPossessAI = EAutoPossessAI::Disabled;

	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->SetCollisionProfileName(TEXT("PhysicsActor"));
	Body->SetNotifyRigidBodyCollision(true);
	Body->BodyInstance.bUseCCD = true;
	RootComponent = Body;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(Body);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		BodyMesh->SetStaticMesh(Cube.Object);   // placeholder box until the MF body is converted
	}

	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(Body);
	CameraArm->bUsePawnControlRotation = true;
	CameraArm->bDoCollisionTest = true;
	CameraArm->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraArm);
}

void AMFVehicle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFVehicle, Health);
	DOREPLIFETIME(AMFVehicle, Driver);
}

void AMFVehicle::BeginPlay()
{
	Super::BeginPlay();
	Body->SetBoxExtent(BoxExtent);
	CameraArm->TargetArmLength = CameraDistance;
	// everything is laid out in MF body space (origin at the MF actor); the box is the root
	if (!MF::ApplyPropMesh(BodyMesh, *BodyMeshName, 1.f, -BoxCenter))
	{
		BodyMesh->SetRelativeScale3D(BoxExtent / 50.f);
	}
	for (const FMFWheelSetup& W : Wheels)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(Body);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->RegisterComponent();
		if (!MF::ApplyPropMesh(C, *W.Mesh, 1.f, W.Position - BoxCenter))
		{
			C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
			C->SetRelativeLocation(W.Position - BoxCenter);
			C->SetRelativeRotation(FRotator(0.f, 0.f, 90.f));
			C->SetRelativeScale3D(FVector(W.Radius / 50.f, W.Radius / 50.f, 0.3f));
		}
		WheelMeshes.Add(C);
	}
	LastCompression.Init(0.f, Wheels.Num());
	WheelSpin.Init(0.f, Wheels.Num());
	Body->OnComponentHit.AddDynamic(this, &AMFVehicle::OnBodyHit);
	if (HasAuthority())
	{
		Body->SetMassOverrideInKg(NAME_None, MassKg, true);
		Body->SetCenterOfMass(CenterOfMass);
		Body->SetLinearDamping(0.02f);
		Body->SetAngularDamping(0.6f);
		Body->SetSimulatePhysics(true);
	}
}

float AMFVehicle::GetSpeedKmh() const
{
	return GetVelocity().Size() * 0.036f;
}

void AMFVehicle::SetDriveInput(float InThrottle, float InSteer, bool bInHandbrake)
{
	Throttle = FMath::Clamp(InThrottle, -1.f, 1.f);
	Steer = FMath::Clamp(InSteer, -1.f, 1.f);
	bHandbrake = bInHandbrake;
	LastInputTime = GetWorld()->GetTimeSeconds();
	if (!HasAuthority())
	{
		ServerDriveInput(Throttle, Steer, bHandbrake);
	}
}

void AMFVehicle::ServerDriveInput_Implementation(float InThrottle, float InSteer, bool bInHandbrake)
{
	SetDriveInput(InThrottle, InSteer, bInHandbrake);
}

void AMFVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && !bBlownUp && Body->IsSimulatingPhysics())
	{
		SimulateWheels(FMath::Min(DeltaSeconds, 0.05f));
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateWheelVisuals(DeltaSeconds);
	}
	// controllers send input only while keys are held: let go of everything otherwise
	if (!Driver || GetWorld()->GetTimeSeconds() - LastInputTime > 0.15f)
	{
		Throttle = Steer = 0.f;
		bHandbrake = !Driver;
	}
}

void AMFVehicle::SimulateWheels(float Dt)
{
	if (!Body->RigidBodyIsAwake() && FMath::IsNearlyZero(Throttle))
	{
		return;   // parked and settled: no traces, no forces
	}
	const FTransform T = Body->GetComponentTransform();
	const FVector Up = T.GetUnitAxis(EAxis::Z);
	const FVector Fwd = T.GetUnitAxis(EAxis::X);
	const float N = FMath::Max(1, Wheels.Num());
	const float WheelMass = MassKg / N;
	const float RestLoad = MassKg * 981.f / N;                       // kg cm/s^2 per wheel
	const float K = SuspensionStiffness * RestLoad / SuspensionTravel; // per cm of compression
	const float C = SuspensionDamping * 2.f * FMath::Sqrt(K * WheelMass);
	const float TopSpeed = TopSpeedKmh / 0.036f;                     // cm/s
	const float EngineForce = MassKg * TopSpeed / AccelTime * 1.4f;
	int32 Driven = 0;
	for (const FMFWheelSetup& W : Wheels)
	{
		Driven += W.bDrive;
	}
	const float Speed = FVector::DotProduct(Body->GetPhysicsLinearVelocity(), Fwd);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFWheel), false, this);
	if (Driver)
	{
		Params.AddIgnoredActor(Driver);
	}
	int32 Grounded = 0;
	for (int32 i = 0; i < Wheels.Num(); ++i)
	{
		const FMFWheelSetup& W = Wheels[i];
		const FVector Top = T.TransformPosition(W.Position - BoxCenter + FVector(0.f, 0.f, SuspensionTravel * 0.5f));
		const float RayLen = SuspensionTravel + W.Radius;
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Top, Top - Up * RayLen, ECC_Visibility, Params))
		{
			LastCompression[i] = 0.f;
			continue;
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
		// sideways grip, weaker on the rear with the handbrake (slides)
		const float GripHere = (bHandbrake && !W.bSteer) ? Grip * 0.25f : Grip;
		const float MaxFriction = 1.3f * Fz;
		float FLat = FMath::Clamp(-VLat * GripHere * WheelMass, -MaxFriction, MaxFriction);
		// engine, brakes, rolling resistance
		float FLong = -VLong * 0.3f * WheelMass;
		const bool bBraking = !FMath::IsNearlyZero(Throttle) && FMath::Abs(Speed) > 60.f && FMath::Sign(Throttle) != FMath::Sign(Speed);
		if (bBraking || bHandbrake)
		{
			const float Brake = BrakeDecel * WheelMass * (bHandbrake ? 1.f : FMath::Abs(Throttle));
			FLong = -FMath::Sign(VLong) * FMath::Min(Brake, FMath::Abs(VLong) * WheelMass / FMath::Max(Dt, 1e-3f));
		}
		else if (W.bDrive && Driven > 0)
		{
			const float Reverse = Throttle < 0.f ? 0.5f : 1.f;   // reverse is slower
			FLong += Throttle * Reverse * EngineForce / Driven;
		}
		FLong = FMath::Clamp(FLong, -MaxFriction * 1.2f, MaxFriction * 1.2f);
		Body->AddForceAtLocation(Up * Fz + WRight * FLat + WFwd * FLong, Contact);
		WheelSpin[i] += VLong / FMath::Max(W.Radius, 1.f) * Dt;
	}
	// air drag caps the top speed; some downforce keeps it planted
	const FVector Vel = Body->GetPhysicsLinearVelocity();
	const float DragK = EngineForce / (TopSpeed * TopSpeed);
	Body->AddForce(-Vel * Vel.Size() * DragK);
	if (Grounded > 0)
	{
		Body->AddForce(-Up * Vel.SizeSquared() * DragK * 0.5f);
	}
	// stuck on its side or roof: back on its wheels after 3 s (MF UpRightingForce)
	if (Up.Z < 0.3f && Vel.Size() < 150.f)
	{
		UpsideDownTime += Dt;
		if (UpsideDownTime > 3.f)
		{
			UpsideDownTime = 0.f;
			const FRotator Rot(0.f, GetActorRotation().Yaw, 0.f);
			Body->SetWorldLocationAndRotation(GetActorLocation() + FVector(0.f, 0.f, 120.f), Rot, false, nullptr, ETeleportType::TeleportPhysics);
			Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		}
	}
	else
	{
		UpsideDownTime = 0.f;
	}
}

void AMFVehicle::UpdateWheelVisuals(float DeltaSeconds)
{
	const FVector Fwd = GetActorForwardVector();
	const float Speed = FVector::DotProduct(GetVelocity(), Fwd);
	for (int32 i = 0; i < WheelMeshes.Num() && i < Wheels.Num(); ++i)
	{
		const FMFWheelSetup& W = Wheels[i];
		if (!HasAuthority())
		{
			WheelSpin[i] += Speed / FMath::Max(W.Radius, 1.f) * DeltaSeconds;
		}
		// suspension drop when not compressed, steering, rolling
		const float Drop = HasAuthority() ? SuspensionTravel * 0.5f - LastCompression[i] : 0.f;
		const FVector Pos = W.Position - BoxCenter - FVector(0.f, 0.f, FMath::Clamp(Drop, -SuspensionTravel * 0.5f, SuspensionTravel * 0.5f));
		WheelMeshes[i]->SetRelativeLocation(Pos);
		const float Yaw = W.bSteer ? Steer * SteerLock : 0.f;
		WheelMeshes[i]->SetRelativeRotation(FRotator(-FMath::RadiansToDegrees(WheelSpin[i]), Yaw, 0.f));
	}
}

// --- seats ------------------------------------------------------------------------------------

bool AMFVehicle::IsNear(const AMFCharacter* C) const
{
	if (!C)
	{
		return false;
	}
	for (int32 D = 0; D < DoorPositions.Num(); ++D)
	{
		if (FVector::Dist(DoorExit(D), C->GetActorLocation()) < 250.f)
		{
			return true;
		}
	}
	return FVector::Dist(GetActorLocation(), C->GetActorLocation()) < BoxExtent.Size() + 120.f;
}

FVector AMFVehicle::DoorExit(int32 Door) const
{
	const FVector Local = DoorPositions.IsValidIndex(Door) ? DoorPositions[Door] : FVector(0.f, BoxExtent.Y + 80.f, 0.f);
	return GetActorTransform().TransformPosition(Local - BoxCenter) + FVector(0.f, 0.f, 60.f);
}

bool AMFVehicle::TryEnter(AMFCharacter* C)
{
	if (!HasAuthority() || bBlownUp || !C || !C->IsAlive() || Driver || !IsNear(C) || C->IsInLoadout())
	{
		return false;
	}
	AController* PC = C->GetController();
	if (!PC)
	{
		return false;
	}
	Driver = C;
	if (AMFWeapon* W = C->GetCurrentWeapon())
	{
		W->ForceInactive();
	}
	C->SetActorEnableCollision(false);
	C->SetActorHiddenInGame(true);
	C->GetCharacterMovement()->DisableMovement();
	C->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	C->SetActorRelativeLocation((SeatPositions.Num() ? SeatPositions[0] : FVector::ZeroVector) - BoxCenter);
	const FRotator View = PC->GetControlRotation();
	PC->Possess(this);
	PC->SetControlRotation(View);
	Body->WakeAllRigidBodies();
	return true;
}

void AMFVehicle::Exit(bool bForce)
{
	AMFCharacter* C = Driver;
	if (!HasAuthority() || !C)
	{
		return;
	}
	// first door with room for a soldier
	FVector Spot = GetActorLocation() + FVector(0.f, 0.f, BoxExtent.Z + 120.f);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(C->GetCapsuleComponent()->GetScaledCapsuleRadius(), C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFVehicleExit), false, this);
	Params.AddIgnoredActor(C);
	bool bFound = false;
	for (int32 D = 0; D < FMath::Max(1, DoorPositions.Num()) && !bFound; ++D)
	{
		const FVector Try = DoorExit(D);
		if (!GetWorld()->OverlapAnyTestByChannel(Try, FQuat::Identity, ECC_Pawn, Shape, Params))
		{
			Spot = Try;
			bFound = true;
		}
	}
	if (!bFound && !bForce)
	{
		return;   // boxed in: stay inside
	}
	AController* PC = GetController();
	Driver = nullptr;
	C->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	C->SetActorHiddenInGame(false);
	C->SetActorEnableCollision(true);
	C->TeleportTo(Spot, FRotator(0.f, GetActorRotation().Yaw, 0.f));
	C->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	C->GetCharacterMovement()->Velocity = GetVelocity() * 0.5f;
	if (PC)
	{
		const FRotator View = PC->GetControlRotation();
		PC->Possess(C);
		PC->SetControlRotation(View);
	}
}

void AMFVehicle::Destroyed()
{
	UE_LOG(LogMF, Verbose, TEXT("Vehicle %s gone at %s"), *GetName(), *GetActorLocation().ToCompactString());
	if (HasAuthority())
	{
		Exit(true);
		if (Generator.IsValid())
		{
			Generator->VehicleGone();
		}
	}
	Super::Destroyed();
}

// --- damage --------------------------------------------------------------------------------------

float AMFVehicle::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// MF: only explosive damage hurts vehicles, scaled by 1 - Armour
	const UMFDamageType* Type = DamageEvent.DamageTypeClass ? Cast<UMFDamageType>(DamageEvent.DamageTypeClass->GetDefaultObject()) : nullptr;
	if (!HasAuthority() || bBlownUp || !Type || !Type->bExplosive)
	{
		return 0.f;
	}
	const int32 Applied = FMath::Max(1, FMath::RoundToInt(Damage * (1.f - Armour)));
	Health -= Applied;
	if (Health <= 0)
	{
		BlowUp(EventInstigator);
	}
	return float(Applied);
}

void AMFVehicle::BlowUp(AController* By)
{
	bBlownUp = true;
	AMFCharacter* Inside = Driver;
	Exit(true);
	if (Inside)
	{
		FDamageEvent Ev(UMFDamage_Explosion::StaticClass());
		Inside->TakeDamage(1000.f, Ev, By, this);
	}
	TArray<AActor*> Ignore{ this };
	UGameplayStatics::ApplyRadialDamageWithFalloff(this, 150.f, 0.f, GetActorLocation(), 0.f, 900.f, 1.f,
		UMFDamage_Explosion::StaticClass(), Ignore, this, By, ECC_Visibility);
	UGameplayStatics::PlaySoundAtLocation(this, LoadObject<USoundBase>(nullptr, TEXT("/Game/MFImported/Sounds/MiscSFX/Explode.Explode"), nullptr, LOAD_NoWarn | LOAD_Quiet), GetActorLocation());
	SetLifeSpan(0.1f);
}

void AMFVehicle::OnBodyHit(UPrimitiveComponent* HitComp, AActor* Other, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// MF Vehicle: hitting someone at KillSpeed or faster runs them over
	AMFCharacter* C = Cast<AMFCharacter>(Other);
	if (!HasAuthority() || !C || !C->IsAlive() || C == Driver)
	{
		return;
	}
	const float Closing = FVector::DotProduct(GetVelocity() - C->GetVelocity(), (C->GetActorLocation() - GetActorLocation()).GetSafeNormal2D());
	if (Closing >= KillSpeed)
	{
		FDamageEvent Ev(UMFDamage_RunOver::StaticClass());
		C->TakeDamage(1000.f, Ev, GetController(), this);
	}
}

// --- generator -------------------------------------------------------------------------------------

AMFVehicleGenerator::AMFVehicleGenerator()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AMFVehicleGenerator::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Generate();
	}
}

void AMFVehicleGenerator::Generate()
{
	if (Spawned.IsValid() || !VehicleClass)
	{
		return;
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
	// VehicleGenerator.CreateVehicle: 100 uu above the generator
	AMFVehicle* V = GetWorld()->SpawnActor<AMFVehicle>(VehicleClass, GetActorLocation() + FVector(0.f, 0.f, 225.f), GetActorRotation(), SP);
	if (V)
	{
		V->Generator = this;
		Spawned = V;
		UE_LOG(LogMF, Verbose, TEXT("Vehicle %s at %s"), *V->GetName(), *V->GetActorLocation().ToCompactString());
	}
	else
	{
		UE_LOG(LogMF, Verbose, TEXT("%s: no room for a %s"), *GetName(), *GetNameSafe(VehicleClass));
		GetWorldTimerManager().SetTimer(Timer, this, &AMFVehicleGenerator::Generate, 5.f, false);   // something parked on it
	}
}

void AMFVehicleGenerator::VehicleGone()
{
	Spawned = nullptr;
	GetWorldTimerManager().SetTimer(Timer, this, &AMFVehicleGenerator::Generate, 5.f, false);
}

void AMFVehicleGenerator::SpawnAll(UWorld* World)
{
	static const TPair<const TCHAR*, UClass*> Map[] = {
		{ TEXT("BuggyGenerator"), AMFVehicle_Buggy::StaticClass() },
		{ TEXT("HumveeGenerator"), AMFVehicle_Humvee::StaticClass() },
		{ TEXT("TruckGenerator"), AMFVehicle_Truck::StaticClass() },
		{ TEXT("ArmadilloGenerator"), AMFVehicle_Armadillo::StaticClass() },
	};
	int32 N = 0;
	for (const auto& Entry : Map)
	{
		for (AMFLegacyActor* L : AMFLegacyActor::FindAll(World, Entry.Key))
		{
			const FString* Auto = L->Properties.Find(TEXT("bAutoGenerate"));
			if (Auto && Auto->Equals(TEXT("False"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AMFVehicleGenerator* G = World->SpawnActorDeferred<AMFVehicleGenerator>(AMFVehicleGenerator::StaticClass(), L->GetActorTransform()))
			{
				G->VehicleClass = Entry.Value;
				G->FinishSpawning(L->GetActorTransform());
				++N;
			}
		}
	}
	UE_LOG(LogMF, Log, TEXT("Vehicles: %d generators"), N);
}

#undef LOCTEXT_NAMESPACE
