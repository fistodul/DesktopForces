#include "AI/MFBotController.h"
#include "DesktopForces.h"
#include "Player/MFCharacter.h"
#include "Game/MFGameMode.h"
#include "Game/MFPlayerState.h"
#include "Core/MFGameSettings.h"
#include "Weapons/MFWeapons.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "TimerManager.h"

AMFBotController::AMFBotController()
{
	bWantsPlayerState = true;
	PrimaryActorTick.bCanEverTick = true;   // AAIController rotates towards the focus in Tick
}

FGenericTeamId AMFBotController::GetGenericTeamId() const
{
	const AMFPlayerState* PS = GetPlayerState<AMFPlayerState>();
	return FGenericTeamId(PS ? PS->GetTeam() : MF::NoTeam);
}

void AMFBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	Enemy = nullptr;
	ReactionDue = 0.f;
	GetWorldTimerManager().SetTimer(ThinkTimer, this, &AMFBotController::Think, 0.2f, true, 0.3f);
}

void AMFBotController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(ThinkTimer);
	Super::OnUnPossess();
}

void AMFBotController::DoLoadout(AMFCharacter* C)
{
	for (const TSoftClassPtr<AMFWeapon>& Item : Profile.Loadout)
	{
		if (UClass* Class = Item.LoadSynchronous())
		{
			C->GiveWeapon(Class, false);
		}
	}
	C->SetInLoadout(false);
	C->SwitchToBestWeapon();
}

AMFCharacter* AMFBotController::FindEnemy(AMFCharacter* C) const
{
	const bool bTeams = C->GetTeam() != MF::NoTeam;
	AMFCharacter* Best = nullptr;
	float BestDist = 9225.f;   // MF PlayerPawn.SightRadius 4100 uu
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
	{
		AMFCharacter* O = *It;
		if (O == C || !O->IsAlive() || O->IsInLoadout() || (bTeams && O->GetTeam() == C->GetTeam()))
		{
			continue;
		}
		const float D = FVector::Dist(O->GetActorLocation(), C->GetActorLocation());
		if (D < BestDist && LineOfSightTo(O))
		{
			BestDist = D;
			Best = O;
		}
	}
	return Best;
}

void AMFBotController::Think()
{

	AMFCharacter* C = Cast<AMFCharacter>(GetPawn());
	if (!C || !C->IsAlive())
	{
		return;
	}
	if (C->IsInLoadout() || C->GetInventory().Num() <= 1)
	{
		DoLoadout(C);   // bots spawning away from a crate still get their favourite kit
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	CheckStuck(C);

	AMFCharacter* Seen = FindEnemy(C);
	if (Seen && Seen != Enemy.Get())
	{
		ReactionDue = Now + FMath::Lerp(0.6f, 0.15f, Skill);   // reaction time
	}
	if (Seen)
	{
		Enemy = Seen;
		LastSeenEnemy = Now;
	}
	else if (Now - LastSeenEnemy > 2.f)
	{
		Enemy = nullptr;
	}

	if (AMFCharacter* Target = Enemy.Get(); Target && Target->IsAlive() && Seen)
	{
		Fight(C, Target);
	}
	else if (AActor* Thing = GetObjectiveTarget())
	{
		// objective to shoot (a safe): face it and keep firing, still walking the goal route
		SetFocus(Thing);
		if (!bHoldingFire && Now >= ReactionDue)
		{
			C->StartFire(false);
			bHoldingFire = true;
		}
		Roam(C);
	}
	else
	{
		C->StopFire(false);
		bHoldingFire = false;
		ClearFocus(EAIFocusPriority::Gameplay);
		Roam(C);
	}
}

void AMFBotController::Fight(AMFCharacter* C, AMFCharacter* Target)
{
	const float Now = GetWorld()->GetTimeSeconds();
	AMFWeapon* W = C->GetCurrentWeapon();
	if (!W || (!W->HasAnyAmmo() && !W->bUndroppable))
	{
		C->SwitchToBestWeapon();
		return;
	}
	// Aim with error that shrinks with skill and grows with distance (MF AdjustAim/AimError)
	const float Dist = FVector::Dist(Target->GetActorLocation(), C->GetActorLocation());
	const float Error = Dist * FMath::Lerp(0.08f, 0.015f, Skill);
	const FVector Aim = Target->GetActorLocation() + FVector(0, 0, 30.f) + FMath::VRand() * Error;
	SetFocalPoint(Aim, EAIFocusPriority::Gameplay);

	// HMG must be deployed before it fires (MF bots: Fire -> ToggleZoom)
	if (AMFWeapon_HeavyMachineGun* HMG = Cast<AMFWeapon_HeavyMachineGun>(W); HMG && !HMG->IsZoomed())
	{
		StopMovement();
		C->StartFire(true);
		C->StopFire(true);
		return;
	}
	if (Now >= ReactionDue)
	{
		if (!W->HasAmmoInClip() && W->CanReload())
		{
			C->StopFire(false);
			C->Reload();
		}
		else
		{
			// Charged weapons (rifle power, grenade throw) fire on release
			const bool bCharged = Cast<AMFWeapon_Rifle>(W) || Cast<AMFWeapon_Grenades>(W);
			const float Hold = Cast<AMFWeapon_Rifle>(W) ? FMath::Lerp(0.5f, 2.5f, Skill) : FMath::Clamp(Dist / 3000.f, 0.2f, 1.1f);
			if (!bHoldingFire)
			{
				C->StartFire(false);
				bHoldingFire = true;
				FireStart = Now;
			}
			else if (bCharged && Now - FireStart >= Hold)
			{
				C->StopFire(false);
				bHoldingFire = false;
			}
		}
	}
	// Strafe around (MF SetupStrafingAbility)
	if (Now >= NextStrafe && !W->IsZoomed())
	{
		NextStrafe = Now + FMath::FRandRange(0.8f, 2.f);
		const FVector Side = FVector::CrossProduct((Target->GetActorLocation() - C->GetActorLocation()).GetSafeNormal(), FVector::UpVector);
		const FVector Goal = C->GetActorLocation() + Side * FMath::FRandRange(-500.f, 500.f);
		if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			FNavLocation Out;
			if (Nav->ProjectPointToNavigation(Goal, Out, FVector(200, 200, 300)))
			{
				MoveToLocation(Out.Location, 50.f, true, true, false, true);
			}
		}
	}
}

bool AMFBotController::TryGoal(AActor* G)
{
	// bStopOnOverlap off: walk onto the goal (capture triggers), not just up to its edge
	const EPathFollowingRequestResult::Type R = MoveToActor(G, 30.f, false);
	if (R == EPathFollowingRequestResult::Failed || (R == EPathFollowingRequestResult::RequestSuccessful && GetPathFollowingComponent()->HasPartialPath()))
	{
		UE_LOG(LogMF, Verbose, TEXT("%s: no full path to %s"), *GetName(), *G->GetName());
		BlockedGoals.Add(G, GetWorld()->GetTimeSeconds() + 30.f);
		StopMovement();
		return false;
	}
	CurrentGoal = G;
	return true;
}

void AMFBotController::SetOrder(EMFBotOrder NewOrder, APawn* Giver)
{
	Order = NewOrder;
	OrderGiver = Giver;
	OrderSpot = Giver ? Giver->GetActorLocation() : (GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector);
	// drop the current plan so the order takes effect on the next think
	CurrentGoal = nullptr;
	LingerUntil = 0.f;
	NextRoam = 0.f;
	StopMovement();
}

AActor* AMFBotController::GetObjectiveTarget() const
{
	const AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>();
	return GM ? GM->GetBotShootTarget(const_cast<AMFBotController*>(this)) : nullptr;
}

void AMFBotController::CheckStuck(AMFCharacter* C)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		StuckCheckTime = Now;
		StuckCheckPos = C->GetActorLocation();
		StuckCount = 0;
		return;
	}
	if (Now - StuckCheckTime < 1.5f)
	{
		return;
	}
	if (FVector::Dist(C->GetActorLocation(), StuckCheckPos) < 40.f)
	{
		// MF bots jumped over what they ran into; after a few tries give the goal up
		C->Jump();
		if (++StuckCount >= 3)
		{
			if (AActor* G = CurrentGoal.Get())
			{
				BlockedGoals.Add(G, Now + 30.f);
			}
			StopMovement();
			CurrentGoal = nullptr;
			NextRoam = 0.f;
			StuckCount = 0;
		}
	}
	else
	{
		StuckCount = 0;
	}
	StuckCheckTime = Now;
	StuckCheckPos = C->GetActorLocation();
}

bool AMFBotController::FollowOrder(AMFCharacter* C)
{
	switch (Order)
	{
	case EMFBotOrder::Hold:
		// "Defend this area": go back to the spot, then stay and watch
		if (FVector::Dist2D(C->GetActorLocation(), OrderSpot) > 250.f)
		{
			if (GetMoveStatus() != EPathFollowingStatus::Moving)
			{
				MoveToLocation(OrderSpot, 100.f);
			}
		}
		return true;
	case EMFBotOrder::Follow:
		// "Cover me": keep close to whoever asked
		if (const APawn* G = OrderGiver.Get())
		{
			if (FVector::Dist(C->GetActorLocation(), G->GetActorLocation()) > 450.f)
			{
				MoveToActor(const_cast<APawn*>(G), 250.f);
			}
			return true;
		}
		Order = EMFBotOrder::Freelance;   // giver gone
		return false;
	default:
		return false;   // Defend / Attack: the game mode's goals take the order into account
	}
}

void AMFBotController::Roam(AMFCharacter* C)
{
	if (FollowOrder(C))
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (GetMoveStatus() == EPathFollowingStatus::Moving)
	{
		// still on the way (CheckStuck handles stalls): every few seconds see whether a more
		// urgent goal came up (our flag dropped, the carrier passed by ...) and switch to it
		if (Now >= NextRoam && CurrentGoal.IsValid())
		{
			NextRoam = Now + 5.f;
			if (const AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>())
			{
				TArray<AActor*> Goals;
				GM->GetBotGoals(this, Goals);
				Goals.RemoveAll([this, Now](AActor* G) { const float* Until = BlockedGoals.Find(G); return Until && Now < *Until; });
				if (Goals.Num() && Goals[0] != CurrentGoal.Get())
				{
					TryGoal(Goals[0]);
				}
			}
		}
		return;
	}
	if (AActor* Prev = CurrentGoal.Get())
	{
		if (LingerUntil == 0.f)
		{
			// Arrived (or gave up) far from the goal: the navmesh doesn't really get there
			const FVector D = Prev->GetActorLocation() - C->GetActorLocation();
			if (D.Size2D() > 150.f && D.Size2D() < 400.f && FMath::Abs(D.Z) < 250.f && !bDirectApproach)
			{
				// path ends short of it (navmesh eroded along a railing / wall): walk straight
				// at it so its trigger is touched (flags, safes, points)
				bDirectApproach = true;
				MoveToLocation(Prev->GetActorLocation(), 20.f, false, false, false);
				return;
			}
			if (D.Size2D() > 150.f || FMath::Abs(D.Z) > 250.f)
			{
				UE_LOG(LogMF, Verbose, TEXT("%s: %s out of reach (%.0f / z %.0f)"), *GetName(), *Prev->GetName(), D.Size2D(), D.Z);
				BlockedGoals.Add(Prev, Now + 30.f);
			}
			else
			{
				LingerUntil = Now + FMath::FRandRange(3.f, 8.f);   // hold it a moment
			}
		}
		if (Now < LingerUntil)
		{
			return;
		}
	}
	CurrentGoal = nullptr;
	LingerUntil = 0.f;
	bDirectApproach = false;
	NextRoam = Now + 15.f;
	// Objective modes hand out goals; once there, linger a little then ask again
	if (const AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>())
	{
		TArray<AActor*> Goals;
		GM->GetBotGoals(this, Goals);
		for (AActor* G : Goals)
		{
			const float* Until = BlockedGoals.Find(G);
			if ((!Until || Now >= *Until) && TryGoal(G))
			{
				NextRoam = Now + 5.f;    // look for a better goal now and then while travelling
				return;
			}
		}
	}
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation Out;
		if (Nav->GetRandomReachablePointInRadius(C->GetActorLocation(), 4500.f, Out))
		{
			const EPathFollowingRequestResult::Type R = MoveToLocation(Out.Location, 100.f);
			UE_CLOG(R == EPathFollowingRequestResult::Failed, LogMF, Verbose, TEXT("%s: roam move failed"), *GetName());
		}
		else
		{
			UE_LOG(LogMF, Verbose, TEXT("%s: no random reachable point near %s"), *GetName(), *C->GetActorLocation().ToCompactString());
		}
	}
}
