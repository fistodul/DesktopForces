#include "Player/DFCharacterMovement.h"
#include "Vehicles/DFVehicle.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "World/DFLadder.h"

UDFCharacterMovement::UDFCharacterMovement()
{
	MaxWalkSpeed = 900.f;                      // MF Pawn.BaseGroundSpeed 400 uu/s
	MaxWalkSpeedCrouched = 270.f;              // UE1 crouch: 0.3 x ground speed
	MaxSwimSpeed = 450.f;                      // MF Pawn.WaterSpeed 200
	Buoyancy = 0.99f;                          // RPlayer.Buoyancy 99 / Mass 100: sinks very slowly
	MaxFlySpeed = 900.f;                       // MF RPlayer.AirSpeed 400
	MaxAcceleration = 4608.f;                  // MF RPlayer.AccelRate 2048
	JumpZVelocity = 731.25f;                   // MF Pawn.JumpZ 325
	AirControl = 0.35f;                        // RPlayer.AirControl
	AirControlBoostMultiplier = 1.f;           // UE1 has no low-speed air control boost
	AirControlBoostVelocityThreshold = 0.f;
	MaxStepHeight = 56.25f;                    // MF Pawn.MaxStepHeight 25
	// UE1 collision is a flat-bottomed cylinder: stand on the rim of grated stair treads
	// instead of sagging into them (warehouse stairs: 54 cm rise, grating 9 cm lower)
	bUseFlatBaseForFloorChecks = true;
	SetWalkableFloorZ(0.7f);                   // UE1 MINFLOORZ
	GravityScale = 1.f;                        // world gravity (project settings) = MF -950 uu/s^2

	// UE1: Velocity -= (Velocity - AccelDir*|V|) * min(2*Friction*dt, 1) while accelerating,
	//      Velocity -= Velocity * 2*Friction*dt when braking. ZoneGroundFriction = 8.
	GroundFriction = 16.f;
	bUseSeparateBrakingFriction = true;
	BrakingFriction = 8.f;
	BrakingFrictionFactor = 2.f;
	BrakingDecelerationWalking = 0.f;
	BrakingDecelerationFalling = 0.f;
	FallingLateralFriction = 0.f;

	LadderSpeed = 450.f;                        // MF Pawn.LadderSpeed 200
	// Bots steer through acceleration like players, so ladder physics (which reads it) works for them
	NavMovementProperties.bUseAccelerationForPaths = true;

	NavAgentProps.bCanCrouch = true;
	NavAgentProps.bCanJump = true;
	bCanWalkOffLedgesWhenCrouching = true;
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;

	// Network: MF servers ran 20-30 Hz; keep client corrections cheap.
	NetworkSimulatedSmoothLocationTime = 0.1f;
}

float UDFCharacterMovement::GetMaxSpeed() const
{
	if (IsOnLadder())
	{
		return LadderSpeed * LadderClimbScale;
	}
	float Speed = Super::GetMaxSpeed();
	if (MovementMode == MOVE_Walking || MovementMode == MOVE_NavWalking)
	{
		// Engine speeds, scaled by carried weight and aiming (both predicted on client and server)
		Speed = (IsCrouching() ? MaxWalkSpeedCrouched : MaxWalkSpeed) * CarrySpeedScale * AimSpeedScale;
		if (bWantsToWalk && !IsCrouching())
		{
			Speed *= WalkSpeedFraction;
		}
	}
	return Speed;
}

void UDFCharacterMovement::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToWalk = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

FNetworkPredictionData_Client* UDFCharacterMovement::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UDFCharacterMovement* Mutable = const_cast<UDFCharacterMovement*>(this);
		Mutable->ClientPredictionData = new FDFNetworkPredictionData_Client(*this);
	}
	return ClientPredictionData;
}

void UDFCharacterMovement::SetLadder(ADFLadderVolume* Ladder)
{
	TouchedLadder = Ladder;
}

void UDFCharacterMovement::ClearLadder(ADFLadderVolume* Ladder)
{
	if (TouchedLadder == Ladder)
	{
		TouchedLadder = nullptr;
		ExitLadder();
	}
}

bool UDFCharacterMovement::WantsLadder() const
{
	const ADFLadderVolume* L = TouchedLadder.Get();
	if (!L || !UpdatedComponent || GetWorld()->GetTimeSeconds() < LadderRegrabTime)
	{
		return false;
	}
	// MF grabs on touch (LadderClimber touch -> PHYS_Ladder): the soldier's cylinder meeting the
	// climber's (EnterLadder then puts them on the rungs at once, no gliding in)
	const FVector Rel = UpdatedComponent->GetComponentLocation() - L->GetActorLocation();
	const float Reach = L->Radius + (CharacterOwner ? CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.f);
	if (Rel.Size2D() > Reach)
	{
		return false;
	}
	if (IsFalling())
	{
		return true;   // walked off the top / jumped at it
	}
	// on the floor: only at the bottom half and pushing towards the rungs, so you can walk away
	// at the foot and step off onto the ledge at the top
	if (Rel.Z > 0.f)
	{
		return false;
	}
	const FVector Into = L->GetActorForwardVector().GetSafeNormal2D();
	return IsMovingOnGround() && FVector::DotProduct(GetCurrentAcceleration().GetSafeNormal2D(), Into) > 0.5f;
}

void UDFCharacterMovement::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
	if (!IsOnLadder() && WantsLadder())
	{
		const ADFLadderVolume* L = TouchedLadder.Get();
		EnterLadder(-L->GetActorForwardVector(), L->ClimbSpeed);
		// straight onto the ladder's line (swept: walls still stop it)
		FVector Offset = L->GetActorLocation() - UpdatedComponent->GetComponentLocation();
		Offset.Z = 0.f;
		if (!Offset.IsNearlyZero())
		{
			FHitResult Hit;
			SafeMoveUpdatedComponent(Offset, UpdatedComponent->GetComponentQuat(), true, Hit);
		}
		Velocity = FVector::ZeroVector;
	}
}

void UDFCharacterMovement::EnterLadder(const FVector& WallNormal, float ClimbSpeedScale)
{
	LadderWallNormal = WallNormal.GetSafeNormal2D();
	LadderClimbScale = ClimbSpeedScale > 0.f ? ClimbSpeedScale * 2.f : 1.f; // LadderClimber.ClimbSpeed 0.5 = normal
	SetMovementMode(MOVE_Custom, uint8(EDFMoveMode::Ladder));
}

void UDFCharacterMovement::ExitLadder()
{
	if (IsOnLadder())
	{
		SetMovementMode(MOVE_Falling);
	}
}

void UDFCharacterMovement::PhysCustom(float DeltaTime, int32 Iterations)
{
	if (!IsOnLadder() || DeltaTime < MIN_TICK_TIME)
	{
		Super::PhysCustom(DeltaTime, Iterations);
		return;
	}

	// PHYS_Ladder: input "forward" (towards the wall) climbs up, backwards climbs down,
	// strafing moves sideways. Looking up/down does not matter (manual tip: "run at them
	// whilst looking straight ahead").
	// Jump lets go of the ladder, pushing away from the wall
	if (CharacterOwner && CharacterOwner->bPressedJump)
	{
		Velocity = LadderWallNormal * 300.f + FVector(0, 0, JumpZVelocity * 0.5f);
		LadderRegrabTime = GetWorld()->GetTimeSeconds() + 0.5f;
		SetMovementMode(MOVE_Falling);
		return;
	}
	const FVector Accel = GetCurrentAcceleration();
	const FVector Into = -LadderWallNormal;
	const float Climb = FVector::DotProduct(Accel.GetSafeNormal(), Into);
	const FVector Side = FVector::CrossProduct(FVector::UpVector, LadderWallNormal);
	const float Strafe = FVector::DotProduct(Accel.GetSafeNormal(), Side);

	// climbing up also presses against the rungs: the wall stops it, past the top it carries
	// the climber onto the ledge
	Velocity = (FVector::UpVector * Climb + Side * Strafe * 0.5f + Into * FMath::Max(Climb, 0.f) * 0.5f) * GetMaxSpeed();
	if (Accel.IsNearlyZero())
	{
		Velocity = FVector::ZeroVector;
	}

	FHitResult Hit;
	SafeMoveUpdatedComponent(Velocity * DeltaTime, UpdatedComponent->GetComponentQuat(), true, Hit);
	if (Hit.IsValidBlockingHit())
	{
		if (Climb < 0.f && Hit.ImpactNormal.Z > GetWalkableFloorZ())
		{
			SetMovementMode(MOVE_Walking);   // reached the floor going down
			return;
		}
		SlideAlongSurface(Velocity * DeltaTime, 1.f - Hit.Time, Hit.Normal, Hit, true);
	}
}

// --- saved move -----------------------------------------------------------------

void FDFSavedMove::Clear()
{
	Super::Clear();
	bSavedWantsToWalk = false;
}

uint8 FDFSavedMove::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();
	if (bSavedWantsToWalk)
	{
		Result |= FLAG_Custom_0;
	}
	return Result;
}

bool FDFSavedMove::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
	if (bSavedWantsToWalk != static_cast<FDFSavedMove*>(NewMove.Get())->bSavedWantsToWalk)
	{
		return false;
	}
	return Super::CanCombineWith(NewMove, Character, MaxDelta);
}

void FDFSavedMove::SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
	if (const UDFCharacterMovement* Move = Cast<UDFCharacterMovement>(Character->GetCharacterMovement()))
	{
		bSavedWantsToWalk = Move->bWantsToWalk;
	}
}

void FDFSavedMove::PrepMoveFor(ACharacter* Character)
{
	Super::PrepMoveFor(Character);
	if (UDFCharacterMovement* Move = Cast<UDFCharacterMovement>(Character->GetCharacterMovement()))
	{
		Move->bWantsToWalk = bSavedWantsToWalk;
	}
}

void UDFCharacterMovement::ApplyImpactPhysicsForces(const FHitResult& Impact, const FVector& ImpactAcceleration, const FVector& ImpactVelocity)
{
	if (!Cast<ADFVehicle>(Impact.GetActor()))
	{
		Super::ApplyImpactPhysicsForces(Impact, ImpactAcceleration, ImpactVelocity);
	}
}

void UDFCharacterMovement::ApplyDownwardForce(float DeltaSeconds)
{
	if (!CurrentFloor.HitResult.IsValidBlockingHit() || !Cast<ADFVehicle>(CurrentFloor.HitResult.GetActor()))
	{
		Super::ApplyDownwardForce(DeltaSeconds);
	}
}
