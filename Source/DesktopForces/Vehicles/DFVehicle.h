#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Info.h"
#include "Core/DFTypes.h"
#include "DFVehicle.generated.h"

class ADFCharacter;
class UBoxComponent;
class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;

/** 'RunDown' - "%k ran over %o." */
UCLASS()
class DESKTOPFORCES_API UDFDamage_RunOver : public UDFDamageType
{
	GENERATED_BODY()
public:
	UDFDamage_RunOver();
};

/** One wheel: where it sits on the body at rest (cm, body space) and how big it is. */
USTRUCT()
struct FDFWheelSetup
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

	/** The flat tyre (MF WheelBlownMesh, "Rage/BuggyWheelFRD"). */
	UPROPERTY(EditDefaultsOnly, Category = "Wheel")
	FString BlownMesh;
};

namespace DF
{
	/** A wheel of MF's own cars (RageGame): Physics/<Vehicle>.COL sphere, cm; the flat tyre
	 *  mesh follows MF's naming (WheelBlownMesh). */
	DESKTOPFORCES_API FDFWheelSetup RageWheel(const FVector& Pos, float Radius, bool bSteer, bool bDrive, const TCHAR* Mesh);
	/** handling.hnd2 MaxSpeed is in mph. */
	constexpr float Mph = 1.609344f;
}

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
class DESKTOPFORCES_API ADFVehicle : public APawn
{
	GENERATED_BODY()

public:
	ADFVehicle();
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
	/** Nitro (MF-mods TurboCars, crouch key; only with ?TurboCars=1). */
	void SetNitro(bool bOn);
	/** The driver's fire buttons (cars ignore them; the heli yaws with them). */
	void SetDriverFire(bool bFire, bool bAlt);
	UFUNCTION(Server, Reliable)
	void ServerSetDriverFire(bool bFire, bool bAlt);
	UFUNCTION(Server, Reliable)
	void ServerSetNitro(bool bOn);

	// --- seats -------------------------------------------------------------------------------
	/** Server: put a soldier in (driver if the driver seat is free). False if full / too far. */
	bool TryEnter(ADFCharacter* C);
	/** Where the team flag stands (MF FlagPos, MF body space, cm); zero: no flag. */
	UPROPERTY(EditDefaultsOnly, Category = "DF|Vehicle")
	FVector FlagPosition = FVector::ZeroVector;

	/** Server: let the driver out at the first free door (else on the roof; forced: even if
	 *  boxed in). */
	void Exit(bool bForce = false);
	/** Server: everyone out, boxed in or not (blown up, locked, gone). */
	void ExitAll();
	/** Server: take a particular passenger seat (1..) if it's free and in reach. */
	bool EnterSeat(ADFCharacter* C, int32 Seat);
	/** Server: let a passenger out (MF passengers sit or stand at aSeatsPos and keep their
	 *  weapons; only the driver can't shoot). False if boxed in and not forced. */
	bool ExitPassenger(ADFCharacter* C, bool bForce = false);
	/** Server: someone died inside: a passenger slides off their seat where they are, the
	 *  driver is put out first (their controller is on the vehicle and goes back to them). */
	void PassengerDied(ADFCharacter* C);
	/** Seat of this soldier: 0 driver, 1.. passengers, INDEX_NONE not inside. */
	int32 GetSeatOf(const ADFCharacter* C) const;
	const TArray<TObjectPtr<ADFCharacter>>& GetPassengers() const { return Passengers; }
	ADFCharacter* GetDriver() const { return Driver; }
	/** Whoever drove it last (still set after they got out; Trailer scoring). */
	ADFCharacter* GetLastDriver() const { return LastDriver.Get(); }
	bool IsNear(const ADFCharacter* C) const;

	/** Server: tow Other behind this one, joined at Hitch (this vehicle's MF body space, cm). */
	void HitchTrailer(ADFVehicle* Other, const FVector& Hitch);
	ADFVehicle* GetTrailer() const { return Trailer; }
	/** Server: brakes on, nobody can drive it any more (MF LockDown). */
	void LockDown();
	bool IsLocked() const { return bLocked; }

	/** Server: blow out a tyre (MF DestroyWheel); it re-inflates after ReInflatePause. */
	void BlowOutWheel(int32 Index);
	bool IsWheelBlown(int32 Index) const { return (BlownWheels >> Index) & 1; }

	/** How much a bot wants to get in (Vehicle.BotDesireability): 1.1, less per flat tyre,
	 *  more with team mates inside, less with enemies; <= 0: not at all (full, wrecked, fast). */
	virtual float BotDesirability(const ADFCharacter* Bot) const;
	/** The seat part of it: 1.1, flat tyres, who's inside; <= 0: full / wrecked / too fast. */
	float SeatDesirability(const ADFCharacter* Bot) const;
	/** A bot gave up driving it (Vehicle.bVehicleStuck): bots leave it alone until then. */
	float BotStuckUntil = -100.f;
	/** Top speed (cm/s), what RoadPoint.MaxSpeed 255 means for a driving bot. */
	float GetTopSpeed() const { return TopSpeedKmh / 0.036f; }
	bool IsUpsideDown() const { return GetActorUpVector().Z < 0.3f; }
	bool IsFloating() const { return Submerged > 0.f; }
	/** Bots drive it along the road graph (not boats: no roads on the water). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	bool bBotsDrive = true;
	bool IsBlownUp() const { return bBlownUp; }

	/** Vehicle health (MF Pawn.Health 100 by default). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Vehicle")
	int32 Health = 100;

	/** HUD readout. */
	float GetSpeedKmh() const;

	/** The physics box (root) and its centre in MF body space: what parts are placed against. */
	UBoxComponent* GetBody() const { return Body; }
	const FVector& GetBoxCenter() const { return BoxCenter; }

	/** Log position, physics state and wheel contacts (DFVehicles cheat). */
	void DebugDump() const;

	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	FText DisplayName;

	/** Which generator made it (respawn when it's gone). */
	TWeakObjectPtr<class AVehicleGenerator> Generator;

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

	/** Shot at, a tyre: spheres round the wheels for weapon traces only (MF WheelCollision). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<class USphereComponent>> WheelColliders;
	/** The petrol cap the same way (MF PetrolCap). */
	UPROPERTY(Transient)
	TObjectPtr<class USphereComponent> PetrolCapCollider;

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
	TArray<FDFWheelSetup> Wheels;

	/** Top speed, km/h (handling MaxSpeed). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float TopSpeedKmh = 110.f;
	/** Seconds from standstill to near top speed. */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float AccelTime = 6.f;
	/** Rolling resistance per second of speed, under throttle / coasting. */
	float RollingDrag = 0.05f;
	float CoastDrag = 0.3f;
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

	/** TurboCars nitro: push forward at this many cm/s^2 up to NitroMaxSpeed (cm/s). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float NitroAccel = 600.f;
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float NitroMaxSpeed = 4000.f;

	/** MF PetrolCapPos / PetrolCapRadius (body space, cm): bullets and pellets there (150) blow it up. */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	FVector PetrolCapPosition = FVector::ZeroVector;
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float PetrolCapRadius = 15.75f;
	/** MF WheelCollision: a tyre takes this much (WheelRadius 20 uu spheres) before it bursts, ... */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	int32 WheelHealth = 35;
	/** ... and re-inflates this long after (MF ReInflatePause 30). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float ReInflatePause = 30.f;

	/** handling.hnd2 Buoyancy: per cent of the body under water when it floats (cars 65, boat 50)
	 *  on the map's PhysicalWaterInfo plane (MF vehicles only feel that plane, not water zones). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float BuoyancyPercent = 65.f;

	/** Engine sounds: <package>.EngineStart / Engine1 (loop, pitch with speed) / EngineStop,
	 *  or each one set ("Sounds/boat/boatEngineRunning"). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	FString EngineSounds;
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	FString EngineStartSound;
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	FString EngineLoopSound;
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	FString EngineStopSound;

	/** Per frame on the server after the wheels (boats: propeller thrust and rudder). */
	virtual void SimulateDrive(float Dt) {}
	float GetWaterPlaneZ() const { return WaterPlaneZ; }

	/** MF Armour: damage taken x (1 - Armour). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float Armour = 0.5f;
	/** Bulletproof hull (Armadillo APCs): bullets and pellets don't hurt it, only its petrol cap. */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	bool bBulletproof = false;
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
	TObjectPtr<ADFCharacter> Driver;

	/** VehicleFlag: a team flag on a pole while a team member drives (Vehicle.SetTeam). */
	void UpdateFlag();
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> FlagPole;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> FlagCloth;
	uint8 FlagTeam = 255;
	FTimerHandle FlagTimer;

	/** Occupants by seat (index = seat; 0 stays empty: that's Driver). */
	UPROPERTY(Replicated)
	TArray<TObjectPtr<ADFCharacter>> Passengers;

	/** Passenger seat taken / given up (hands it to the vehicle's UDFVehicleTurrets). */
	virtual void PassengerEntered(ADFCharacter* C, int32 Seat);
	virtual void PassengerLeft(ADFCharacter* C, int32 Seat);
	void SeatPassenger(ADFCharacter* C, int32 Seat);
	void UnseatPassenger(ADFCharacter* C);
	bool FindExitSpot(const ADFCharacter* C, FVector& OutSpot) const;

	TWeakObjectPtr<ADFCharacter> LastDriver;
	UPROPERTY(Transient)
	TObjectPtr<ADFVehicle> Trailer;
	UPROPERTY(Transient)
	TObjectPtr<class UPhysicsConstraintComponent> Hitch;
	bool bLocked = false;

	float Throttle = 0.f;
	float Steer = 0.f;
	bool bHandbrake = false;
	float LastInputTime = -100.f;
	bool bNitro = false;
	bool bTurbo = false;
	bool bDriverFire = false;
	bool bDriverAltFire = false;
	/** Engine loop pitch (1 = as recorded). Car: SoundPitch 64 + 2 x speed / 60 uu/s. */
	virtual float GetEnginePitch() const;
	TArray<float> LastCompression;
	TArray<float> WheelSpin;
	float UpsideDownTime = 0.f;
	bool bBlownUp = false;
	/** PhysicalWaterInfo.WaterHeight (cm); none: -inf. Submerged share of the body (0..1). */
	float WaterPlaneZ = -1e9f;
	float Submerged = 0.f;
	void ApplyWater(float Dt);

	/** Flat tyres, one bit per wheel (replicated: the flat mesh, the car pulls). */
	UPROPERTY(ReplicatedUsing = OnRep_BlownWheels)
	uint8 BlownWheels = 0;
	UFUNCTION()
	void OnRep_BlownWheels();
	/** The flat tyres this machine has puffed smoke for. */
	uint8 ShownBlownWheels = 0;
	TArray<int32> WheelDamage;
	TArray<FTimerHandle> ReInflateTimers;
	int32 PetrolCapHealth = 150;

	/** Engine running loop (clients; MF AmbientSound in state Driving). */
	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> EngineAudio;
	bool bEngineOn = false;
	FTimerHandle EngineStartTimer;
	float BumpSoundUntil = 0.f;
	void UpdateEngineSound();
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBump(float Volume);
	/** Wheel.HitWall: a tyre landing hard thumps (WheelSFX.HitWheel). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastWheelThump(float Volume);
	uint8 AirborneWheels = 0;
	float WheelThumpUntil = 0.f;

	/** Sparks and a clang where it was hit (MF Vehicle.TakeDamage: HitMetal; tyres HitRubber). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastHitFX(FVector_NetQuantize At, bool bTyre);
	/** Blown up: the bang and 16 frags flying (Vehicle.BlowUp, RageEffects.VehicleFrag). */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastBlowUpFX();
	/** MF damage rules plus the server's changeDamage mutator (ADFGameMode::VehicleDamage...). */
	int32 ScaleDamage(float Damage, const class UDFDamageType* Type, AController* By) const;
	void ApplyHitPush(const FDamageEvent& DamageEvent, AActor* DamageCauser);

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
class DESKTOPFORCES_API AVehicleGenerator : public AActor
{
	GENERATED_BODY()

public:
	AVehicleGenerator();

	UPROPERTY(EditAnywhere, Category = "Vehicle")
	TSubclassOf<ADFVehicle> VehicleClass;

	void VehicleGone();

	/** Spawn generators for every legacy *Generator actor in the world (server), except within 8 m of Exclude. */
	static void SpawnAll(UWorld* World, const TArray<FVector>& Exclude = TArray<FVector>());
	/** "heli.heli", "Rage.Truck", "piper.piperTs" ... -> the game's class (null if unknown). */
	static UClass* ClassForMF(const FString& MFClass);

protected:
	virtual void BeginPlay() override;
	void Generate();

	TWeakObjectPtr<ADFVehicle> Spawned;
	FTimerHandle Timer;
};
