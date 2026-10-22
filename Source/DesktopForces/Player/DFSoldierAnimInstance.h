// Soldier animation the UE1 way: one sequence at a time, tweened into from the pose before
// (PlayAnim / LoopAnim / TweenAnim), the sequence picked by game code (MF Pawn.uc Get*Anim)
// instead of an anim graph. No assets beyond the imported sequences.
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

UCLASS(Transient)
class DESKTOPFORCES_API UDFSoldierAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Sequences are "<SequencePrefix><Name>" (the importer names them after the mesh). */
	void SetSequencePrefix(const FString& Prefix) { SequencePrefix = Prefix; Sequences.Reset(); }

	/** UE1 PlayAnim / LoopAnim: play Name at Rate, tweening from the current pose over
	 *  TweenTime. A looping sequence already playing just keeps going. False if no such sequence. */
	bool Play(FName Name, float Rate, float TweenTime, bool bLoop);

	FName GetCurrentName() const { return CurrentName; }
	/** A non-looping sequence still has frames to play. */
	bool IsPlayingOnce() const;

	/** Events from the pawn (all machines that show it). */
	void OnFire(bool bAlt);
	void OnDeath();

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FDFSoldierAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete InProxy; }
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UAnimSequence* Find(FName Name);
	/** MF Pawn.uc: the movement sequence for the pawn's state, weapon and stance. */
	FName PickMovement(class ADFCharacter* C, bool& bOutLoop, float& OutTween);

	friend struct FDFSoldierAnimProxy;
	FDFAnimChannel Current, Previous;
	float Alpha = 1.f;
	float TweenTime = 0.f;
	FName CurrentName;
	FString SequencePrefix;
	bool bWasFalling = false;
	bool bDead = false;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UAnimSequence>> Sequences;
};
