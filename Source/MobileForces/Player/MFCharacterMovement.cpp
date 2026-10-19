#include "Player/MFCharacterMovement.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

UMFCharacterMovement::UMFCharacterMovement()
{
	MaxWalkSpeed = 900.f;                      // MF Pawn.BaseGroundSpeed 400 uu/s
	MaxWalkSpeedCrouched = 270.f;              // UE1 crouch: 0.3 x ground speed
	MaxSwimSpeed = 450.f;                      // MF Pawn.WaterSpeed 200
	MaxFlySpeed = 900.f;                       // MF RPlayer.AirSpeed 400
	MaxAcceleration = 4608.f;                  // MF RPlayer.AccelRate 2048
	JumpZVelocity = 731.25f;                   // MF Pawn.JumpZ 325
	AirControl = 0.35f;                        // RPlayer.AirControl
	AirControlBoostMultiplier = 1.f;           // UE1 has no low-speed air control boost
	AirControlBoostVelocityThreshold = 0.f;
	MaxStepHeight = 56.25f;                    // MF Pawn.MaxStepHeight 25
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

float UMFCharacterMovement::GetMaxSpeed() const
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

void UMFCharacterMovement::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToWalk = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

FNetworkPredictionData_Client* UMFCharacterMovement::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UMFCharacterMovement* Mutable = const_cast<UMFCharacterMovement*>(this);
		Mutable->ClientPredictionData = new FMFNetworkPredictionData_Client(*this);
	}
	return ClientPredictionData;
}

void UMFCharacterMovement::EnterLadder(const FVector& WallNormal, float ClimbSpeedScale)
{
	LadderWallNormal = WallNormal.GetSafeNormal2D();
	LadderClimbScale = ClimbSpeedScale > 0.f ? ClimbSpeedScale * 2.f : 1.f; // LadderClimber.ClimbSpeed 0.5 = normal
	SetMovementMode(MOVE_Custom, uint8(EMFMoveMode::Ladder));
}

void UMFCharacterMovement::ExitLadder()
{
	if (IsOnLadder())
	{
		SetMovementMode(MOVE_Falling);
	}
}

void UMFCharacterMovement::PhysCustom(float DeltaTime, int32 Iterations)
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
		SetMovementMode(MOVE_Falling);
		return;
	}
	const FVector Accel = GetCurrentAcceleration();
	const FVector Into = -LadderWallNormal;
	const float Climb = FVector::DotProduct(Accel.GetSafeNormal(), Into);
	const FVector Side = FVector::CrossProduct(FVector::UpVector, LadderWallNormal);
	const float Strafe = FVector::DotProduct(Accel.GetSafeNormal(), Side);

	Velocity = (FVector::UpVector * Climb + Side * Strafe * 0.5f) * GetMaxSpeed();
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

void FMFSavedMove::Clear()
{
	Super::Clear();
	bSavedWantsToWalk = false;
}

uint8 FMFSavedMove::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();
	if (bSavedWantsToWalk)
	{
		Result |= FLAG_Custom_0;
	}
	return Result;
}

bool FMFSavedMove::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
	if (bSavedWantsToWalk != static_cast<FMFSavedMove*>(NewMove.Get())->bSavedWantsToWalk)
	{
		return false;
	}
	return Super::CanCombineWith(NewMove, Character, MaxDelta);
}

void FMFSavedMove::SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
	if (const UMFCharacterMovement* Move = Cast<UMFCharacterMovement>(Character->GetCharacterMovement()))
	{
		bSavedWantsToWalk = Move->bWantsToWalk;
	}
}

void FMFSavedMove::PrepMoveFor(ACharacter* Character)
{
	Super::PrepMoveFor(Character);
	if (UMFCharacterMovement* Move = Cast<UMFCharacterMovement>(Character->GetCharacterMovement()))
	{
		Move->bWantsToWalk = bSavedWantsToWalk;
	}
}
