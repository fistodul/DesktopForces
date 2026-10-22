// Animation the UE1 way: one sequence at a time, tweened into from the pose before (PlayAnim /
// LoopAnim / TweenAnim), the sequence picked by game code (for the soldiers MF Pawn.uc's
// Get*Anim, for the first-person arms the weapon's state) instead of an anim graph. No assets
// beyond the imported sequences.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "DFSoldierAnimInstance.generated.h"

class UAnimSequence;

/** One playing sequence. */
struct FDFAnimChannel
{
	UAnimSequence* Seq = nullptr;
	float Time = 0.f;
	float Rate = 1.f;
	bool bLoop = false;
};

/** Worker-thread side: samples the current sequence and blends in the one it tweens from. */
struct FDFSoldierAnimProxy : public FAnimInstanceProxy
{
	FDFSoldierAnimProxy() = default;
	explicit FDFSoldierAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	FDFAnimChannel Current, Previous;
	float Alpha = 1.f;   // weight of Current
};

/** UE1-style animation player: plays one sequence, tweens from the pose before it, falls back
 *  to an idle loop when a one-shot ends. The first-person arms use it as it is. */
UCLASS(Transient)
class DESKTOPFORCES_API UDFTweenAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Sequences are "<SequencePrefix><Name>" (the importer names them after the mesh). */
	void SetSequencePrefix(const FString& Prefix) { SequencePrefix = Prefix; Sequences.Reset(); }

	/** UE1 PlayAnim / LoopAnim: play Name at Rate, tweening from the current pose over
	 *  TweenTime. A looping sequence already playing just keeps going. False if no such sequence. */
	bool Play(FName Name, float Rate, float TweenTime, bool bLoop);
	/** Play Name once, at the rate that makes it last Duration (a game state's length). */
	bool PlayFitted(FName Name, float Duration, float TweenTime);
	/** The loop to go back to when a one-shot is done (none: hold the last frame). */
	void SetIdle(FName Name, float InTweenTime = 0.25f) { IdleName = Name; IdleTween = InTweenTime; }
	bool HasSequence(FName Name) { return Find(Name) != nullptr; }

	FName GetCurrentName() const { return CurrentName; }
	/** A non-looping sequence still has frames to play. */
	bool IsPlayingOnce() const;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FDFSoldierAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete InProxy; }
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UAnimSequence* Find(FName Name);

	friend struct FDFSoldierAnimProxy;
	FDFAnimChannel Current, Previous;
	float Alpha = 1.f;
	float TweenTime = 0.f;
	FName CurrentName;
	FName IdleName;
	float IdleTween = 0.25f;
	FString SequencePrefix;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UAnimSequence>> Sequences;
};

/** The soldiers: picks the sequence the way MF's Pawn.uc does from the pawn's state. */
UCLASS(Transient)
class DESKTOPFORCES_API UDFSoldierAnimInstance : public UDFTweenAnimInstance
{
	GENERATED_BODY()

public:
	/** Events from the pawn (all machines that show it). */
	void OnFire(bool bAlt);
	void OnReload(float Duration);
	/** RPlayer.PlayTakeHit: a flinch (GutHit / LeftHit / RightHit pose, tweened in and out). */
	void OnHurt();
	void OnDeath();

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	/** MF Pawn.uc: the movement sequence for the pawn's state, weapon and stance. */
	FName PickMovement(class ADFCharacter* C, bool& bOutLoop, float& OutTween);

	bool bWasFalling = false;
	bool bDead = false;
};
