#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MFCharacterMovement.generated.h"

/** Custom movement modes (MOVE_Custom + this). */
UENUM()
enum class EMFMoveMode : uint8
{
	Ladder = 0,   // UE1 PHYS_Ladder (LadderClimber volumes)
};

/**
 * Character movement tuned to Mobile Forces' UE1 player physics.
 *
 * UE5's walking physics descends from UE1's PHYS_Walking, so most parameters map
 * one to one. Values are UE cm; comments give the MF original (1 MF uu = 2.25 cm):
 *   GroundSpeed 400, AccelRate 2048, AirControl 0.35, JumpZ 325, MaxStepHeight 25,
 *   ZoneGroundFriction 8, gravity -950 (project physics settings).
 * UE1 applies friction twice as strongly (Velocity -= Velocity * 2 * Friction * dt)
 * which is reproduced with GroundFriction 16 / BrakingFriction 8 * factor 2.
 *
 * Speed modifiers (multiplicative): carried weight (Pawn.SetSpeedFromInventory),
 * walking / crouching (x0.3, UE1 native), aiming down sights, deployed HMG.
 */
UCLASS()
class MOBILEFORCES_API UMFCharacterMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UMFCharacterMovement();

	/** Multiplier from carried inventory blocks: 1 - 0.05 * blocks, min 0.5. Replicated via the character. */
	UPROPERTY(Transient)
	float CarrySpeedScale = 1.f;

	/** Multiplier while aiming down sights / zoomed (MF: "move slowly to maintain accuracy"). */
	UPROPERTY(Transient)
	float AimSpeedScale = 1.f;

	/** Walking (shift) speed fraction (UE1: 0.3). Crouching uses the engine's MaxWalkSpeedCrouched. */
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	float WalkSpeedFraction = 0.3f;

	/** Set by the owning client; replicated through compressed flags. */
	bool bWantsToWalk = false;

	// --- ladders (LadderClimber) ---
	UPROPERTY(EditDefaultsOnly, Category = "MF|Ladder")
	float LadderSpeed;

	/** Touching a ladder volume / no longer touching it (overlap events of AMFLadderVolume). */
	void SetLadder(class AMFLadderVolume* Ladder);
	void ClearLadder(class AMFLadderVolume* Ladder);
	void EnterLadder(const FVector& WallNormal, float ClimbSpeedScale);
	void ExitLadder();
	bool IsOnLadder() const { return MovementMode == MOVE_Custom && CustomMovementMode == uint8(EMFMoveMode::Ladder); }

	virtual float GetMaxSpeed() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;

protected:
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	/** Grab the touched ladder now? (falling into it, or walking at it while close) */
	bool WantsLadder() const;

	TWeakObjectPtr<class AMFLadderVolume> TouchedLadder;
	FVector LadderWallNormal = FVector::ZeroVector;
	float LadderClimbScale = 1.f;
	/** World time before which a released ladder can't be grabbed again (jumped off). */
	float LadderRegrabTime = 0.f;
};

/** Saved move carrying the walk flag (FLAG_Custom_0). */
class FMFSavedMove : public FSavedMove_Character
{
public:
	using Super = FSavedMove_Character;
	bool bSavedWantsToWalk = false;

	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* Character) override;
};

class FMFNetworkPredictionData_Client : public FNetworkPredictionData_Client_Character
{
public:
	using Super = FNetworkPredictionData_Client_Character;
	explicit FMFNetworkPredictionData_Client(const UCharacterMovementComponent& ClientMovement) : Super(ClientMovement) {}
	virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FMFSavedMove()); }
};
