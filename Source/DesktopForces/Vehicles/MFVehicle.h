#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Info.h"
#include "Core/MFTypes.h"
#include "MFVehicle.generated.h"

class AMFCharacter;
class UBoxComponent;
class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;

/** 'RunDown' - "%k ran over %o." */
UCLASS()
class DESKTOPFORCES_API UMFDamage_RunOver : public UMFDamageType
{
	GENERATED_BODY()
public:
	UMFDamage_RunOver();
};

/** One wheel: where it sits on the body at rest (cm, body space) and how big it is. */
USTRUCT()
struct FMFWheelSetup
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Wheel")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, Category = "Wheel")
	float Radius = 40.f;

	UPROPERTY(EditDefaultsOnly, Category = "Wheel")
	bool bSteer = false;

	UPROPERTY(EditDefaultsOnly, Category = "Wheel")
	bool bDrive = true;

	/** Converted MF wheel mesh ("Rage/BuggyWheelFR"); mirrored for the other side when bMirror. */
	UPROPERTY(EditDefaultsOnly, Category = "Wheel")
	FString Mesh;
};

/**
 * Mobile Forces vehicle (MF Vehicle / Car): a physics box with raycast wheels - suspension
 * springs, engine force on the driven wheels, brakes, sideways grip - tuned from MF's own
 * handling file (Physics/handling.hnd2: mass, top speed, steering lock, brakes, ride height)
 * and wheel spheres (Physics/<Vehicle>.COL). Cheap: a handful of line traces per frame
 * while something moves, asleep when parked.
 *
 * Seats: E (TryEnterExit) near the vehicle gets in; the first free seat drives (the player's
 * controller possesses the vehicle; the soldier rides hidden). Bullets do nothing to vehicles
 * (MF: only explosives hurt them, scaled by 1 - Armour); at KillSpeed or more it runs people
 * over. Blows up at 0 health, taking whoever is inside.
 */
UCLASS(Abstract, Config = Game)
class DESKTOPFORCES_API AMFVehicle : public APawn
{
	GENERATED_BODY()

public:
	AMFVehicle();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override {}
	virtual void Destroyed() override;

	// --- driving (input from the player controller / bots) ---------------------------------
	/** Throttle -1..1 (negative brakes, then reverses) and steering -1..1. */
	void SetDriveInput(float InThrottle, float InSteer, bool bInHandbrake);
	UFUNCTION(Server, Unreliable)
	void ServerDriveInput(float InThrottle, float InSteer, bool bInHandbrake);

	// --- seats -------------------------------------------------------------------------------
	/** Server: put a soldier in (driver if the driver seat is free). False if full / too far. */
	bool TryEnter(AMFCharacter* C);
	/** Server: let the driver out at the nearest free door. */
	void Exit(bool bForce = false);
	/** Server: take a particular passenger seat (1..) if it's free and in reach. */
	bool EnterSeat(AMFCharacter* C, int32 Seat);
	/** Server: let a passenger out (MF passengers sit or stand at aSeatsPos and keep their
	 *  weapons; only the driver can't shoot). False if boxed in and not forced. */
	bool ExitPassenger(AMFCharacter* C, bool bForce = false);
	/** Server: a passenger died in their seat (they slide off it where they are). */
	void PassengerDied(AMFCharacter* C);
	/** Seat of this soldier: 0 driver, 1.. passengers, INDEX_NONE not inside. */
	int32 GetSeatOf(const AMFCharacter* C) const;
	const TArray<TObjectPtr<AMFCharacter>>& GetPassengers() const { return Passengers; }
	AMFCharacter* GetDriver() const { return Driver; }
	/** Whoever drove it last (still set after they got out; Trailer scoring). */
	AMFCharacter* GetLastDriver() const { return LastDriver.Get(); }
	bool IsNear(const AMFCharacter* C) const;

	/** Server: tow Other behind this one, joined at Hitch (this vehicle's MF body space, cm). */
	void HitchTrailer(AMFVehicle* Other, const FVector& Hitch);
	AMFVehicle* GetTrailer() const { return Trailer; }
	/** Server: brakes on, nobody can drive it any more (MF LockDown). */
	void LockDown();
	bool IsLocked() const { return bLocked; }

	/** Vehicle health (MF Pawn.Health 100 by default). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Vehicle")
	int32 Health = 100;

	/** HUD readout. */
	float GetSpeedKmh() const;

	/** Log position, physics state and wheel contacts (MFVehicles cheat). */
	void DebugDump() const;

	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	FText DisplayName;

	/** Which generator made it (respawn when it's gone). */
	TWeakObjectPtr<class AMFVehicleGenerator> Generator;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Vehicle")
	TObjectPtr<UBoxComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Vehicle")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "Vehicle")
	TObjectPtr<USpringArmComponent> CameraArm;

	UPROPERTY(VisibleAnywhere, Category = "Vehicle")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> WheelMeshes;

	// --- tuning (constructors of the vehicle classes, MF values in comments) -------------------
	/** Has an engine and a driver's seat (a trailer doesn't). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	bool bDriveable = true;

	/** Converted MF body mesh ("Rage/BuggyMesh"); a box stands in without it. */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	FString BodyMeshName;

	/** Physics box: half size and centre (body space, cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	FVector BoxExtent = FVector(150.f, 80.f, 50.f);
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	FVector BoxCenter = FVector(0.f, 0.f, 60.f);

	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float MassKg = 800.f;
	/** Centre of mass offset from the box centre (cm); low keeps it on its wheels. */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	FVector CenterOfMass = FVector(0.f, 0.f, -40.f);

	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	TArray<FMFWheelSetup> Wheels;

	/** Top speed, km/h (handling MaxSpeed). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float TopSpeedKmh = 110.f;
	/** Seconds from standstill to near top speed. */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float AccelTime = 6.f;
	/** Steering lock, degrees (handling Lock). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float SteerLock = 35.f;
	/** Brake deceleration, cm/s^2. */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float BrakeDecel = 1200.f;
	/** Suspension travel (cm) and how stiff / damped (1 = holds the car at half travel). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float SuspensionTravel = 30.f;
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float SuspensionStiffness = 2.f;
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float SuspensionDamping = 0.35f;
	/** Sideways grip (fraction of the slide cancelled per second-ish). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float Grip = 6.f;

	/** MF Armour: explosive damage taken x (1 - Armour). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float Armour = 0.5f;
	/** MF KillSpeed 8 (m/s): faster than this it runs people over. */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float KillSpeed = 800.f;

	/** Where riders sit (body space, cm; seat 0 drives) and get out (doors). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	TArray<FVector> SeatPositions;
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	TArray<FVector> DoorPositions;

	/** Camera distance (MF NearCamera, uu x 2.25). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float CameraDistance = 560.f;

	// --- state ---------------------------------------------------------------------------
	UPROPERTY(Replicated)
	TObjectPtr<AMFCharacter> Driver;

	/** Occupants by seat (index = seat; 0 stays empty: that's Driver). */
	UPROPERTY(Replicated)
	TArray<TObjectPtr<AMFCharacter>> Passengers;

	/** Passenger seat taken / given up (ArmedAPC: the gunner seat). */
	virtual void PassengerEntered(AMFCharacter* C, int32 Seat) {}
	virtual void PassengerLeft(AMFCharacter* C, int32 Seat) {}
	void SeatPassenger(AMFCharacter* C, int32 Seat);
	void UnseatPassenger(AMFCharacter* C);
	bool FindExitSpot(const AMFCharacter* C, FVector& OutSpot) const;

	TWeakObjectPtr<AMFCharacter> LastDriver;
	UPROPERTY(Transient)
	TObjectPtr<AMFVehicle> Trailer;
	UPROPERTY(Transient)
	TObjectPtr<class UPhysicsConstraintComponent> Hitch;
	bool bLocked = false;

	float Throttle = 0.f;
	float Steer = 0.f;
	bool bHandbrake = false;
	float LastInputTime = -100.f;
	TArray<float> LastCompression;
	TArray<float> WheelSpin;
	float UpsideDownTime = 0.f;
	bool bBlownUp = false;

	UFUNCTION()
	void OnBodyHit(UPrimitiveComponent* HitComp, AActor* Other, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	void SimulateWheels(float DeltaSeconds);
	void UpdateWheelVisuals(float DeltaSeconds);
	void BlowUp(AController* By);
	FVector DoorExit(int32 Door) const;
};

/**
 * Makes a vehicle at a MF VehicleGenerator (BuggyGenerator, HumveeGenerator ...) and a new
 * one 5 s after it's gone (VehicleGenerator: NormalState Waiting). Placed by the game mode
 * from the map's legacy generator actors.
 */
UCLASS()
class DESKTOPFORCES_API AMFVehicleGenerator : public AActor
{
	GENERATED_BODY()

public:
	AMFVehicleGenerator();

	UPROPERTY(EditAnywhere, Category = "Vehicle")
	TSubclassOf<AMFVehicle> VehicleClass;

	void VehicleGone();

	/** Spawn generators for every legacy *Generator actor in the world (server), except within 8 m of Exclude. */
	static void SpawnAll(UWorld* World, const TArray<FVector>& Exclude = TArray<FVector>());

protected:
	virtual void BeginPlay() override;
	void Generate();

	TWeakObjectPtr<AMFVehicle> Spawned;
	FTimerHandle Timer;
};
