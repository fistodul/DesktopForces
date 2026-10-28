#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DFCharacterMovement.generated.h"

/** Custom movement modes (MOVE_Custom + this). */
UENUM()
enum class EDFMoveMode : uint8 {
	Ladder = 0, // UE1 PHYS_Ladder (LadderClimber volumes)
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
class DESKTOPFORCES_API UDFCharacterMovement : public UCharacterMovementComponent {
	GENERATED_BODY()

public:
	UDFCharacterMovement();

	/** Multiplier from carried inventory blocks: 1 - 0.05 * blocks, min 0.5. Replicated via the character. */
	UPROPERTY(Transient)
	float CarrySpeedScale = 1.f;

	/** Multiplier while aiming down sights / zoomed (MF: "move slowly to maintain accuracy"). */
	UPROPERTY(Transient)
	float AimSpeedScale = 1.f;

	/** Walking (shift) speed fraction (UE1: 0.3). Crouching uses the engine's MaxWalkSpeedCrouched. */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float WalkSpeedFraction = 0.3f;

	/** Set by the owning client; replicated through compressed flags. */
	bool bWantsToWalk = false;
	/** Jump held (the owning client's input; FLAG_Custom_1): opens the parachute while falling. */
	bool bWantsParachute = false;

	// --- ladders (LadderClimber) ---
	UPROPERTY(EditDefaultsOnly, Category = "DF|Ladder")
	float LadderSpeed;

	/** Touching a ladder volume / no longer touching it (overlap events of ADFLadderVolume). */
	void SetLadder(class ADFLadderVolume* Ladder);
	void ClearLadder(class ADFLadderVolume* Ladder);
	void EnterLadder(const FVector& WallNormal, float ClimbSpeedScale);
	void ExitLadder();
	bool IsOnLadder() const { return MovementMode == MOVE_Custom && CustomMovementMode == uint8(EDFMoveMode::Ladder); }

	virtual float GetMaxSpeed() const override;
	/** Soldiers don't shove vehicles about by walking into or standing on them (MF). */
	virtual void ApplyImpactPhysicsForces(const FHitResult& Impact, const FVector& ImpactAcceleration, const FVector& ImpactVelocity) override;
	virtual void ApplyDownwardForce(float DeltaSeconds) override;
	virtual void ApplyRepulsionForce(float DeltaSeconds) override;
	virtual void CapsuleTouched(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;

protected:
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	/** Under a parachute: the fall eased to 200 uu/s, gliding the way the soldier looks. */
	virtual void PhysFalling(float DeltaTime, int32 Iterations) override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	/** Grab the touched ladder now? (falling into it, or walking at it while close) */
	bool WantsLadder() const;

	TWeakObjectPtr<class ADFLadderVolume> TouchedLadder;
	FVector LadderWallNormal = FVector::ZeroVector;
	float LadderClimbScale = 1.f;
	/** World time before which a released ladder can't be grabbed again (jumped off). */
	float LadderRegrabTime = 0.f;
};

/** Saved move carrying the walk (FLAG_Custom_0) and parachute (FLAG_Custom_1) flags. */
class FDFSavedMove : public FSavedMove_Character {
public:
	using Super = FSavedMove_Character;
	bool bSavedWantsToWalk = false;
	bool bSavedWantsParachute = false;

	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* Character) override;
};

class FDFNetworkPredictionData_Client : public FNetworkPredictionData_Client_Character {
public:
	using Super = FNetworkPredictionData_Client_Character;
	explicit FDFNetworkPredictionData_Client(const UCharacterMovementComponent& ClientMovement)
		: Super(ClientMovement) {}
	virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FDFSavedMove()); }
};
