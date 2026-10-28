#include "Player/DFSoldierAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/DFCharacter.h"
#include "Components/BoxComponent.h"
#include "Vehicles/DFVehicle.h"
#include "Weapons/DFWeapon.h"

namespace {
	// MF Pawn.uc Get*Anim, by Weapon.AnimNumber 1-10 (knife, pistol, machine gun, shotgun, rifle,
	// heavy machine gun, launcher, grenades, trip bombs, MG-alike): [pointing (zoomed), not]
	using FAnimRow = const TCHAR* [10];
	const FAnimRow StillAnims[2] = {
		{ TEXT("KnifeStillAlt"), TEXT("PistolStillLg"), TEXT("MgunStillLg"), TEXT("ShotStillLg"), TEXT("SnipeStillLg"), TEXT("HmachGStillLg"), TEXT("LaunchStillLg"), TEXT("GrenStillAlt"), TEXT("TripStillLg"), TEXT("MgunStillLg") },
		{ TEXT("KnifeStillSm"), TEXT("PistolStillSm"), TEXT("MgunStillSm"), TEXT("ShotStillLg"), TEXT("SnipeStillSm"), TEXT("HmachGStillLg"), TEXT("LaunchStillSm"), TEXT("GrenStillSm"), TEXT("TripStillLg"), TEXT("MgunStillSm") }
	};
	const FAnimRow RunAnims[2] = {
		{ TEXT("KnifeRunAlt"), TEXT("PistolRunLg"), TEXT("MgunRunLg"), TEXT("ShotRunLg"), TEXT("SnipeRunLg"), TEXT("HmachGRunLg"), TEXT("LaunchRunLg"), TEXT("GrenRunAlt"), TEXT("TripRunLg"), TEXT("MgunRunLg") },
		{ TEXT("KnifeRunSm"), TEXT("PistolRunSm"), TEXT("MgunRunSm"), TEXT("ShotRunLg"), TEXT("SnipeRunSm"), TEXT("HmachGRunLg"), TEXT("LaunchRunSm"), TEXT("GrenRunSm"), TEXT("TripRunLg"), TEXT("MgunRunSm") }
	};
	const FAnimRow BackrunAnims[2] = {
		{ TEXT("KnifeBackrunAlt"), TEXT("PistolBackrunLg"), TEXT("MgunBackrunLg"), TEXT("ShotBackrunLg"), TEXT("SnipeBackrunLg"), TEXT("HmachGBackrunLg"), TEXT("LaunchBackrunLg"), TEXT("GrenBackrunAlt"), TEXT("TripBackrunLg"), TEXT("MgunBackrunLg") },
		{ TEXT("KnifeBackrunSm"), TEXT("PistolBackrunSm"), TEXT("MgunBackrunLg"), TEXT("ShotBackrunLg"), TEXT("SnipeBackrunSm"), TEXT("HmachGBackrunLg"), TEXT("LaunchBackrunSm"), TEXT("GrenBackrunSm"), TEXT("TripBackrunLg"), TEXT("MgunBackrunLg") }
	};
	const FAnimRow StrafeLeftAnims[2] = {
		{ TEXT("KnifeStrafeLeftAlt"), TEXT("PistolStrafeLeftLg"), TEXT("MgunStrafeLeftLg"), TEXT("ShotStrafeLeftLg"), TEXT("SnipeStrafeLeftLg"), TEXT("HmachGStrafeLeftLg"), TEXT("LaunchStrafeLeftLg"), TEXT("GrenStrafeLeftAlt"), TEXT("TripStrafeLeftLg"), TEXT("MgunStrafeLeftLg") },
		{ TEXT("KnifeStrafeLeftSm"), TEXT("PistolStrafeLeftSm"), TEXT("MgunStrafeLeftSm"), TEXT("ShotStrafeLeftLg"), TEXT("SnipeStrafeLeftSm"), TEXT("HmachGStrafeLeftLg"), TEXT("LaunchStrafeLeftSm"), TEXT("GrenStrafeLeftSm"), TEXT("TripStrafeLeftSm"), TEXT("MgunStrafeLeftSm") }
	};
	const FAnimRow StrafeRightAnims[2] = {
		{ TEXT("KnifeStrafeRightAlt"), TEXT("PistolStrafeRightLg"), TEXT("MgunStrafeRightLg"), TEXT("ShotStrafeRightLg"), TEXT("SnipeStrafeRightLg"), TEXT("HmachGStrafeRightLg"), TEXT("LaunchStrafeRightLg"), TEXT("GrenStrafeRightAlt"), TEXT("TripStrafeRightLg"), TEXT("MgunStrafeRightLg") },
		{ TEXT("KnifeStrafeRightSm"), TEXT("PistolStrafeRightSm"), TEXT("MgunStrafeRightSm"), TEXT("ShotStrafeRightLg"), TEXT("SnipeStrafeRightSm"), TEXT("HmachGStrafeRightLg"), TEXT("LaunchStrafeRightSm"), TEXT("GrenStrafeRightSm"), TEXT("TripStrafeRightSm"), TEXT("MgunStrafeRightSm") }
	};
	const FAnimRow WalkAnims[2] = {
		{ TEXT("KnifeWalkAlt"), TEXT("PistolWalkLg"), TEXT("MgunWalkLg"), TEXT("ShotWalkLg"), TEXT("SnipeWalkLg"), TEXT("HmachGWalkLg"), TEXT("LaunchWalkLg"), TEXT("GrenWalkAlt"), TEXT("TripWalkLg"), TEXT("MgunWalkLg") },
		{ TEXT("KnifeWalkSm"), TEXT("PistolWalkSm"), TEXT("MgunWalkSm"), TEXT("ShotWalkLg"), TEXT("SnipeWalkSm"), TEXT("HmachGWalkLg"), TEXT("LaunchWalkSm"), TEXT("GrenWalkSm"), TEXT("TripWalkLg"), TEXT("MgunWalkSm") }
	};
	const FAnimRow DuckAnims = { TEXT("KnifeDuckStillAlt"), TEXT("PistolDuckStillLg"), TEXT("MgunDuckStillLg"), TEXT("ShotDuckStillLg"), TEXT("SnipeDuckStillLg"), TEXT("HmachGDuckStillLg"), TEXT("LaunchDuckStillLg"), TEXT("GrenDuckStillAlt"), TEXT("TripDuckStillLg"), TEXT("MgunDuckStillLg") };
	const FAnimRow CrawlAnims = { TEXT("KnifeDuckWalkAlt"), TEXT("PistolDuckWalkLg"), TEXT("MgunDuckWalkLg"), TEXT("ShotDuckWalkLg"), TEXT("SnipeDuckWalkLg"), TEXT("HmachGDuckWalkLg"), TEXT("LaunchDuckWalkLg"), TEXT("GrenDuckWalkAlt"), TEXT("TripDuckWalkLg"), TEXT("MgunDuckWalkLg") };
	const FAnimRow JumpAnims = { TEXT("KnifeJumpAlt"), TEXT("PistolJumpLg"), TEXT("MgunJumpLg"), TEXT("ShotJumpLg"), TEXT("SnipeJumpLg"), TEXT("HmachGJumpLg"), TEXT("LaunchJumpLg"), TEXT("GrenJumpAlt"), TEXT("TripJumpLg"), TEXT("PistolJumpLg") };
	const FAnimRow LandAnims = { TEXT("KnifeLandAlt"), TEXT("PistolLandLg"), TEXT("MgunLandLg"), TEXT("ShotLandLg"), TEXT("SnipeLandLg"), TEXT("HmachGLandLg"), TEXT("LaunchLandLg"), TEXT("GrenLandAlt"), TEXT("TripLandLg"), TEXT("PistolLandLg") };
	// GetFireAnim / GetAltFireAnim (only knife, grenades and trip bombs have one): [crouched,
	// walking, running, crouched still, still] for sets 1, 8, 9
	const TCHAR* const FireAnims[2][5][3] = {
		{ { TEXT("KnifeDuckStillFireSm"), TEXT("GrenDuckWalkThrowAlt"), TEXT("TripDuckWalkArmLg") },
			{ TEXT("KnifeFireSm"), TEXT("GrenWalkThrowAlt"), TEXT("TripWalkArmLg") },
			{ TEXT("KnifeRunFireSm"), TEXT("GrenRunThrowAlt"), TEXT("TripRunArmLg") },
			{ TEXT("KnifeDuckStillFireSm"), TEXT("GrenDuckStillThrowAlt"), TEXT("TripDuckStillArmLg") },
			{ TEXT("KnifeFireSm"), TEXT("GrenStillThrowAlt"), TEXT("TripStillArmLg") } },
		{ { TEXT("KnifeDuckStillFireAlt"), TEXT("GrenDuckWalkThrowAlt"), TEXT("TripDuckWalkThrowSm") },
			{ TEXT("KnifeFireAlt"), TEXT("GrenWalkThrowAlt"), TEXT("TripWalkThrowSm") },
			{ TEXT("KnifeFireAlt"), TEXT("GrenRunThrowAlt"), TEXT("TripRunThrowSm") },
			{ TEXT("KnifeDuckStillFireAlt"), TEXT("GrenDuckStillThrowAlt"), TEXT("TripDuckStillThrowSm") },
			{ TEXT("KnifeFireAlt"), TEXT("GrenStillThrowAlt"), TEXT("TripStillThrowSm") } }
	};
	// GetReloadAnim (guns only): [crouched, standing]
	const FAnimRow ReloadAnims[2] = {
		{ nullptr, TEXT("PistolDuckReload"), TEXT("MgunDuckReload"), TEXT("ShotDuckReload"), TEXT("SnipeDuckReload"), nullptr, TEXT("LaunchDuckReloadLg"), nullptr, nullptr, TEXT("MgunDuckReload") },
		{ nullptr, TEXT("PistolWalkReload"), TEXT("MgunWalkReload"), TEXT("ShotWalkReloadLg"), TEXT("SnipeWalkReload"), nullptr, TEXT("LaunchWalkReload"), nullptr, nullptr, TEXT("MgunWalkReload") }
	};
	// RPlayer.PlayDying (Dead1 and Dead9 are commented out there)
	const TCHAR* const DeathAnims[] = { TEXT("Dead2"), TEXT("Dead3"), TEXT("Dead4"), TEXT("Dead5"), TEXT("Dead9B") };

	int32 AnimSetOf(const ADFCharacter* C) {
		const ADFWeapon* W = C->GetCurrentWeapon();
		const int32 Set = W ? W->GetSoldierAnimSet() : 0;
		return Set >= 1 && Set <= 10 ? Set - 1 : -1;
	}
} // namespace

// --- proxy ---------------------------------------------------------------------------------

void FDFSoldierAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) {
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	const UDFTweenAnimInstance* I = CastChecked<UDFTweenAnimInstance>(InAnimInstance);
	Current = I->Current;
	Previous = I->Previous;
	Alpha = I->Alpha;
}

bool FDFSoldierAnimProxy::Evaluate(FPoseContext& Output) {
	auto Sample = [](const FDFAnimChannel& Ch, FPoseContext& Into) {
		FAnimationPoseData Data(Into);
		Ch.Seq->GetAnimationPose(Data, FAnimExtractContext(double(Ch.Time), false, {}, Ch.bLoop));
	};
	if (!Current.Seq) {
		Output.ResetToRefPose();
		return true;
	}
	Sample(Current, Output);
	if (Alpha < 1.f && Previous.Seq) {
		FPoseContext From(Output);
		Sample(Previous, From);
		FAnimationPoseData Out(Output);
		FAnimationRuntime::BlendTwoPosesTogetherInPlace(Out, FAnimationPoseData(From), Alpha);
	}
	return true;
}

// --- game thread ---------------------------------------------------------------------------

UAnimSequence* UDFTweenAnimInstance::Find(FName Name) {
	if (const TObjectPtr<UAnimSequence>* Found = Sequences.Find(Name))
		return *Found;
	const FString Asset = SequencePrefix + Name.ToString();
	UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s.%s"), *Asset, *FPackageName::GetShortName(Asset)), nullptr, LOAD_NoWarn | LOAD_Quiet);
	Sequences.Add(Name, Seq); // misses too (MF names a few sequences that don't exist)
	return Seq;
}

bool UDFTweenAnimInstance::Play(FName Name, float Rate, float InTweenTime, bool bLoop) {
	if (Name == CurrentName && bLoop && Current.bLoop) {
		Current.Rate = Rate;
		return true;
	}
	UAnimSequence* Seq = Find(Name);
	if (!Seq)
		return false;
	Previous = Current;
	Current = { Seq, 0.f, Rate, bLoop };
	CurrentName = Name;
	TweenTime = InTweenTime;
	Alpha = InTweenTime > 0.f && Previous.Seq ? 0.f : 1.f;
	return true;
}

bool UDFTweenAnimInstance::PlayFitted(FName Name, float Duration, float InTweenTime) {
	const UAnimSequence* Seq = Find(Name);
	if (!Seq)
		return false;
	const float Rate = Duration > 0.f ? FMath::Clamp(Seq->GetPlayLength() / Duration, 0.25f, 8.f) : 1.f;
	CurrentName = NAME_None; // a one-shot restarts even if it is already the current one
	return Play(Name, Rate, InTweenTime, false);
}

bool UDFTweenAnimInstance::IsPlayingOnce() const {
	return Current.Seq && !Current.bLoop && Current.Time < Current.Seq->GetPlayLength();
}

void UDFTweenAnimInstance::NativeUpdateAnimation(float DeltaSeconds) {
	Super::NativeUpdateAnimation(DeltaSeconds);
	// advance: the current sequence plays (or loops); the one tweened from stays put (UE1 tweens
	// from a pose, not a moving sequence)
	if (Current.Seq) {
		const float Len = Current.Seq->GetPlayLength();
		Current.Time += DeltaSeconds * Current.Rate;
		Current.Time = Current.bLoop && Len > 0.f ? FMath::Fmod(Current.Time, Len) : FMath::Min(Current.Time, Len);
	}
	if (Alpha < 1.f)
		Alpha = TweenTime > 0.f ? FMath::Min(1.f, Alpha + DeltaSeconds / TweenTime) : 1.f;
	if (!IdleName.IsNone() && CurrentName != IdleName && !IsPlayingOnce())
		Play(IdleName, 1.f, IdleTween, true);
}

void UDFSoldierAnimInstance::OnFire(bool bAlt) {
	ADFCharacter* C = Cast<ADFCharacter>(TryGetPawnOwner());
	const int32 Set = C ? AnimSetOf(C) : -1;
	const int32 Col = Set == 0 ? 0 : Set == 7 ? 1
		: Set == 8                            ? 2
											  : -1;
	if (Col < 0 || bDead)
		return; // guns have no firing animation: they shoot from the pose they hold
	const bool bMoving = C->GetVelocity().Size2D() > 20.f;
	const bool bCrouched = C->bIsCrouched;
	const int32 Row = bCrouched ? (bMoving ? 0 : 3) : !bMoving                       ? 4
		: C->GetVelocity().Size2D() < C->GetCharacterMovement()->MaxWalkSpeed * 0.5f ? 1
																					 : 2;
	Play(FireAnims[bAlt ? 1 : 0][Row][Col], 1.f, 0.1f, false);
}

void UDFSoldierAnimInstance::OnReload(float Duration) {
	ADFCharacter* C = Cast<ADFCharacter>(TryGetPawnOwner());
	const int32 Set = C ? AnimSetOf(C) : -1;
	if (Set < 0 || bDead || C->GetAttachParentActor())
		return;
	if (const TCHAR* Name = ReloadAnims[C->bIsCrouched ? 0 : 1][Set])
		PlayFitted(Name, Duration, 0.2f);
}

void UDFSoldierAnimInstance::OnHurt() {
	ADFCharacter* C = Cast<ADFCharacter>(TryGetPawnOwner());
	if (!C || bDead || C->IsFeigningDeath() || IsPlayingOnce() || C->GetAttachParentActor())
		return;
	static const TCHAR* const Hits[] = { TEXT("Guthit"), TEXT("Guthit"), TEXT("Lefthit"), TEXT("Righthit") };
	Play(Hits[FMath::RandHelper(UE_ARRAY_COUNT(Hits))], 1.f, 0.08f, false);
}

void UDFSoldierAnimInstance::OnDeath() {
	bDead = true;
	Play(DeathAnims[FMath::RandHelper(UE_ARRAY_COUNT(DeathAnims))], 1.f, 0.1f, false);
}

FName UDFSoldierAnimInstance::PickMovement(ADFCharacter* C, bool& bOutLoop, float& OutTween) {
	const UCharacterMovementComponent* M = C->GetCharacterMovement();
	const int32 Set = AnimSetOf(C);
	const ADFWeapon* W = C->GetCurrentWeapon();
	const int32 Point = W && W->IsZoomed() ? 0 : 1;
	const FVector V = C->GetVelocity();
	const float Speed = V.Size2D();
	bOutLoop = true;
	OutTween = 0.15f;
	if (const ADFVehicle* Veh = Cast<ADFVehicle>(C->GetAttachParentActor())) {
		// Pawn.PlayVehicleDriving(Left / Right), GetStillAnim's sitting passengers; standing
		// riders (gunners, the truck bed) fall through to the plain still pose
		const int32 Seat = Veh->GetSeatOf(C);
		if (Seat == 0) {
			const float Turn = Veh->GetBody() ? Veh->GetBody()->GetPhysicsAngularVelocityInDegrees().Z : 0.f;
			OutTween = 0.5f;
			return Veh->GetVelocity().Size() < 50.f ? TEXT("DriveStill") : Turn > 20.f ? TEXT("DriveRight")
				: Turn < -20.f                                                         ? TEXT("DriveLeft")
																					   : TEXT("Drive");
		}
		if (Seat > 0 && Veh->IsSeatSitting(Seat)) {
			OutTween = 0.2f;
			return TEXT("PassLgStill");
		}
	}
	if (W && W->IsDeployed()) {
		// HeavyMachineGun: down behind the gun on its tripod (TweenAnim HmachGStill, held)
		bOutLoop = false;
		OutTween = 0.5f;
		return TEXT("HmachGStill");
	}
	if (M->IsSwimming()) {
		const bool bSmall = Set < 0 || Set == 0 || Set == 1 || Set >= 7;
		return Speed > 20.f ? (bSmall ? TEXT("SwimSM") : TEXT("SwimLG")) : (bSmall ? TEXT("TreadSM") : TEXT("TreadLG"));
	}
	if (M->IsFalling()) {
		// PlayInAir: tween to the jump pose and hold it
		bOutLoop = false;
		OutTween = 0.25f;
		return Set < 0 ? TEXT("KnifeJumpAlt") : JumpAnims[Set];
	}
	if (C->bIsCrouched)
		return Set < 0 ? (Speed > 20.f ? TEXT("KnifeDuckWalkAlt") : TEXT("KnifeDuckStillAlt")) : (Speed > 20.f ? CrawlAnims[Set] : DuckAnims[Set]);
	if (Speed < 20.f)
		return Set < 0 ? TEXT("KnifeStillSm") : StillAnims[Point][Set];
	if (Speed < M->MaxWalkSpeed * 0.5f) // walking (shift) is 0.3 of running
		return Set < 0 ? TEXT("KnifeWalkAlt") : WalkAnims[Point][Set];
	// GetRunAnim: by where the pawn moves against where it faces
	const FVector Dir = V.GetSafeNormal2D();
	const float Fwd = Dir | C->GetActorForwardVector();
	if (Fwd < -0.75f)
		return Set < 0 ? TEXT("KnifeBackrunSm") : BackrunAnims[Point][Set];
	if (Fwd < 0.75f) {
		const bool bRight = (Dir | C->GetActorRightVector()) > 0.f;
		return Set < 0 ? (bRight ? TEXT("KnifeStrafeRightAlt") : TEXT("KnifeStrafeLeftSm")) : (bRight ? StrafeRightAnims[Point][Set] : StrafeLeftAnims[Point][Set]);
	}
	return Set < 0 ? TEXT("MgunRunLg") : RunAnims[Point][Set];
}

void UDFSoldierAnimInstance::NativeUpdateAnimation(float DeltaSeconds) {
	Super::NativeUpdateAnimation(DeltaSeconds);

	ADFCharacter* C = Cast<ADFCharacter>(TryGetPawnOwner());
	if (!C || bDead)
		return;
	if (C->IsFeigningDeath()) {
		if (CurrentName != TEXT("DeathEnd"))
			Play(TEXT("DeathEnd"), 1.f, 0.5f, false); // RPlayer.PlayFeignDeath
		return;
	}
	const bool bFalling = C->GetCharacterMovement()->IsFalling();
	if (bWasFalling && !bFalling && !C->bIsCrouched) {
		// PlayLanded: TweenAnim(GetLandAnim(), 0.12)
		const int32 Set = AnimSetOf(C);
		Play(Set < 0 ? TEXT("KnifeLandAlt") : LandAnims[Set], 1.f, 0.12f, false);
	}
	bWasFalling = bFalling;
	if (IsPlayingOnce() && !bFalling)
		return; // a throw, stab or landing plays out
	bool bLoop = true;
	float Tween = 0.15f;
	const FName Want = PickMovement(C, bLoop, Tween);
	if (Want != CurrentName)
		Play(Want, 1.f, Tween, bLoop);
}
