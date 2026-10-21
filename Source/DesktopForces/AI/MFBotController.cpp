#include "AI/MFBotController.h"
#include "Game/MFGameState.h"
#include "DesktopForces.h"
#include "Player/MFCharacter.h"
#include "Game/MFGameMode.h"
#include "Game/MFPlayerState.h"
#include "Core/MFGameSettings.h"
#include "Weapons/MFWeapons.h"
#include "Vehicles/MFVehicle.h"
#include "World/MFRoadNetwork.h"
#include "Components/CapsuleComponent.h"
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
	AMFCharacter* C = Cast<AMFCharacter>(InPawn);
	if (Cast<AMFVehicle>(InPawn))
	{
		// took the wheel: plan the road on the next think
		Route.Reset();
		DriveStart = GetWorld()->GetTimeSeconds();
		SlowSince = -1.f;
		ReverseUntil = 0.f;
		DriveStuckCount = 0;
		ClearFocus(EAIFocusPriority::Gameplay);
	}
	else if (C && C == Soldier.Get())
	{
		// out of the vehicle, same life: walk the rest, picking the goal afresh
		CurrentGoal = nullptr;
		LingerUntil = 0.f;
		NextRoam = 0.f;
		GoingForVehicle = nullptr;
	}
	else
	{
		Soldier = C;
		Enemy = nullptr;
		bDidLoadout = false;
		ReactionDue = 0.f;
		GoingForVehicle = nullptr;
		DriveGoal = nullptr;
		if (C)
		{
			DecideVehicleUse(C);
		}
	}
	GetWorldTimerManager().SetTimer(ThinkTimer, this, &AMFBotController::Think, 0.2f, true, 0.3f);
}

void AMFBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (AMFVehicle* V = Cast<AMFVehicle>(GetPawn()))
	{
		Steer(V);   // vehicles drop input not renewed within 0.15 s
	}
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
		const AMFGameState* GS = GetWorld()->GetGameState<AMFGameState>();
		if (UClass* Class = Item.LoadSynchronous(); Class && (!GS || GS->IsItemAllowed(Class)))
		{
			C->GiveWeapon(Class, false);
		}
	}
	C->SetInLoadout(false);
	C->SwitchToBestWeapon();
	bDidLoadout = true;
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
	if (AMFVehicle* V = Cast<AMFVehicle>(GetPawn()))
	{
		Drive(V);
		return;
	}
	AMFCharacter* C = Cast<AMFCharacter>(GetPawn());
	if (!C || !C->IsAlive())
	{
		return;
	}
	if (C->IsInLoadout() || (!bDidLoadout && C->GetInventory().Num() <= 1))
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

	AMFVehicle* Riding = Cast<AMFVehicle>(C->GetAttachParentActor());
	if (Riding && !Ride(C, Riding))
	{
		Riding = nullptr;   // got out
	}
	if (AMFCharacter* Target = Enemy.Get(); Target && Target->IsAlive() && Seen)
	{
		Fight(C, Target);
	}
	else if (Riding)
	{
		C->StopFire(false);
		bHoldingFire = false;
		ClearFocus(EAIFocusPriority::Gameplay);
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
	if (W->IsMeleeWeapon())
	{
		// knives (zombies): run at them, slash when in reach
		MoveToActor(Target, 60.f, true, true, false);
		if (Dist < 260.f && Now >= ReactionDue)
		{
			C->StartFire(false);
			C->StopFire(false);
		}
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
			// ... and get in with them (RageBot: Orders 'Follow', OrderObject.VehicleIn)
			if (AMFVehicle* GV = Cast<AMFVehicle>(G->GetAttachParentActor()))
			{
				if (C->GetAttachParentActor() != GV && GV->BotDesirability(C) > 0.f && GetWorld()->GetTimeSeconds() >= DontEnterVehicleTime)
				{
					if (GoingForVehicle.Get() != GV)
					{
						GoingForVehicle = GV;
						GoingForVehicleSince = GetWorld()->GetTimeSeconds();
					}
					GoToVehicle(C);
				}
				return true;
			}
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
	if (GoingForVehicle.IsValid() && GoToVehicle(C))
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
			if (AActor* G = CurrentGoal.Get())
			{
				PickVehicle(C, G, G->GetActorLocation());   // a vehicle turned up on the way
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
				PickVehicle(C, G, G->GetActorLocation());
				return;
			}
		}
	}
	// MF bots ranged over the whole map (FindSpecialAttraction): now and then a far road node,
	// by car if there's one about
	const AMFRoadNetwork* Net = AMFRoadNetwork::Get(GetWorld());
	if (bUseVehicles && Net && Net->Nodes.Num() && FMath::FRand() < 0.3f)
	{
		const FVector Far = Net->Nodes[FMath::RandRange(0, Net->Nodes.Num() - 1)];
		if (FVector::Dist(Far, C->GetActorLocation()) > 6000.f && MoveToLocation(Far, 200.f) != EPathFollowingRequestResult::Failed)
		{
			PickVehicle(C, nullptr, Far);
			return;
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

// --- vehicles ------------------------------------------------------------------------------

void AMFBotController::DecideVehicleUse(const AMFCharacter* C)
{
	// RageBot.SetBehaviour: 4 in 5 bots drive in deathmatch; in a team game each of a small
	// team's bots does 4 times in 5, in a bigger one at most 3 in 4 of them
	const uint8 Team = C->GetTeam();
	if (Team == MF::NoTeam)
	{
		bUseVehicles = FMath::FRand() <= 0.8f;
		return;
	}
	int32 Total = 0, Using = 0;
	for (TActorIterator<AMFBotController> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->GetGenericTeamId() == FGenericTeamId(Team))
		{
			++Total;
			Using += It->bUseVehicles ? 1 : 0;
		}
	}
	bUseVehicles = Total <= 2 ? FMath::FRand() <= 0.8f : Using <= Total * 0.75f;
}

bool AMFBotController::PickVehicle(AMFCharacter* C, AActor* Goal, const FVector& Dest)
{
	// RageBot.PickVehcicle
	const float Now = GetWorld()->GetTimeSeconds();
	const AMFRoadNetwork* Net = AMFRoadNetwork::Get(GetWorld());
	if (!bUseVehicles || !Net || GoingForVehicle.IsValid() || Now < DontEnterVehicleTime || !C->GetCharacterMovement()->IsMovingOnGround())
	{
		return false;
	}
	// how much road the trip takes (it counted the bRoad nodes on its route): more than 10
	// nodes (Level.MinimumRoadDistanceAttack) is worth a vehicle, more than 18 is far away
	TArray<int32> Road;
	const int32 From = Net->FindNearestNode(C->GetActorLocation(), 4500.f, C);
	const int32 To = Net->FindNearestNode(Dest, 4500.f, Goal);
	if (From == INDEX_NONE || To == INDEX_NONE || !Net->FindRoute(From, To, Road) || Road.Num() <= 10)
	{
		return false;
	}
	const bool bFar = Road.Num() > 18;
	const float FinalDist = FVector::Dist(Dest, C->GetActorLocation());
	const float MaxZ = C->GetActorLocation().Z + C->GetCharacterMovement()->MaxStepHeight + C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	AMFVehicle* Best = nullptr;
	float BestWeight = 0.f;
	for (TActorIterator<AMFVehicle> It(GetWorld()); It; ++It)
	{
		const float D = FVector::Dist(It->GetActorLocation(), C->GetActorLocation());
		const float* Blocked = BlockedGoals.Find(*It);
		float W = It->BotDesirability(C);
		if (W <= 0.f || D >= 5625.f || It->GetActorLocation().Z > MaxZ || (Blocked && Now < *Blocked))   // MaxB 2500 uu; not on a floor above
		{
			continue;
		}
		W *= (1.f - D / 5625.f) * (bFar ? 1.f : 0.5f);                                    // nearer: better
		W *= FMath::Min(FinalDist + (bFar ? 2250.f : 0.f), 22500.f) / 22500.f * 2.f;      // further to go: better
		if (W > BestWeight)
		{
			BestWeight = W;
			Best = *It;
		}
	}
	if (!Best)
	{
		return false;
	}
	UE_LOG(LogMF, Verbose, TEXT("%s: going for %s (%d road nodes to %s)"), *GetName(), *Best->GetName(), Road.Num(), *Dest.ToCompactString());
	GoingForVehicle = Best;
	GoingForVehicleSince = Now;
	DriveGoal = Goal;
	DriveDest = Dest;
	return GoToVehicle(C);
}

bool AMFBotController::GoToVehicle(AMFCharacter* C)
{
	const float Now = GetWorld()->GetTimeSeconds();
	AMFVehicle* V = GoingForVehicle.Get();
	if (!V || V->BotDesirability(C) <= 0.f || Now - GoingForVehicleSince > 25.f)
	{
		UE_CLOG(V, LogMF, Verbose, TEXT("%s: gave up on %s (%s, %.0f cm away)"), *GetName(), *V->GetName(),
			V->BotDesirability(C) <= 0.f ? TEXT("not wanted") : TEXT("took too long"), FVector::Dist(V->GetActorLocation(), C->GetActorLocation()));
		if (V && V->BotDesirability(C) > 0.f)
		{
			DontEnterVehicleTime = Now + 10.f;   // couldn't get there: carry on on foot for a bit
			BlockedGoals.Add(V, Now + 30.f);
		}
		GoingForVehicle = nullptr;
		return false;
	}
	if (V->IsNear(C))
	{
		GoingForVehicle = nullptr;
		StopMovement();
		if (!V->TryEnter(C))   // the driver's seat if free, else the first free passenger seat
		{
			DontEnterVehicleTime = Now + 5.f;
			return false;
		}
		UE_LOG(LogMF, Verbose, TEXT("%s: in %s (seat %d)"), *GetName(), *V->GetName(), V->GetSeatOf(C));
		return true;
	}
	// walk right up to it (stopping on overlap would count its whole collision radius and end
	// further out than IsNear reaches); the navmesh often stops short of a parking bay, so the
	// last stretch is a straight walk
	if (GetMoveStatus() == EPathFollowingStatus::Moving)
	{
		return true;
	}
	const bool bClose = FVector::Dist(V->GetActorLocation(), C->GetActorLocation()) < 2500.f;
	const EPathFollowingRequestResult::Type R = bClose ? MoveToLocation(V->GetActorLocation(), 50.f, false, false, false, false) : MoveToActor(V, 50.f, false, true, false);
	const FNavPathSharedPtr Path = GetPathFollowingComponent()->GetPath();
	const bool bShort = !bClose && GetPathFollowingComponent()->HasPartialPath() && Path.IsValid()
		&& FVector::Dist(Path->GetEndLocation(), V->GetActorLocation()) > 2500.f;   // ends too far off for the straight walk
	if (R == EPathFollowingRequestResult::Failed || bShort)
	{
		BlockedGoals.Add(V, Now + 30.f);
		UE_LOG(LogMF, Verbose, TEXT("%s: no path to %s (from %s to %s, %s)"), *GetName(), *V->GetName(), *C->GetActorLocation().ToCompactString(),
			*V->GetActorLocation().ToCompactString(), R == EPathFollowingRequestResult::Failed ? TEXT("failed") : *FString::Printf(TEXT("ends %.0f cm off"), FVector::Dist(Path->GetEndLocation(), V->GetActorLocation())));
		StopMovement();
		GoingForVehicle = nullptr;
		DontEnterVehicleTime = Now + 5.f;
		return false;
	}
	return true;
}

bool AMFBotController::Ride(AMFCharacter* C, AMFVehicle* V)
{
	// AttackingVehicle: shoot from the seat (Think does); PrepareToLeaveVehicle once the
	// driver is gone and it stopped, it's in the water or about to blow, or (following) the
	// one we follow got out
	const APawn* G = Order == EMFBotOrder::Follow ? OrderGiver.Get() : nullptr;
	const bool bOver = (!V->GetDriver() && V->GetVelocity().Size() < 200.f) || V->IsFloating() || V->Health < 25
		|| (G && G->GetAttachParentActor() != V);
	if (bOver && V->ExitPassenger(C))
	{
		UE_LOG(LogMF, Verbose, TEXT("%s: off %s"), *GetName(), *V->GetName());
		DontEnterVehicleTime = GetWorld()->GetTimeSeconds() + 1.5f;
		return false;
	}
	return true;
}

void AMFBotController::Drive(AMFVehicle* V)
{
	// GotoPlaceVehicle: from road node to road node to the one nearest the goal, then out
	const float Now = GetWorld()->GetTimeSeconds();
	const AMFRoadNetwork* Net = AMFRoadNetwork::Get(GetWorld());
	if (const AActor* G = DriveGoal.Get())
	{
		DriveDest = G->GetActorLocation();
	}
	const AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>();
	ChaseTarget = GM ? GM->GetBotDriveTarget(this) : nullptr;
	// PrepareToLeaveVehicle: landed in water, on its roof, about to blow
	if (V->IsFloating() || V->IsUpsideDown() || V->Health < 25 || (!ChaseTarget.IsValid() && (!Net || Now - DriveStart > 180.f)))
	{
		LeaveVehicle(V, TEXT("water / roof / wrecked / too long"));
		return;
	}
	if (const AActor* Chase = ChaseTarget.Get())
	{
		// in plain sight: straight at it; else the road towards it (FindPathTowardVeh),
		// planned again when it has moved away from the end of the route
		FCollisionQueryParams Clear(SCENE_QUERY_STAT(MFBotChase), false, V);
		Clear.AddIgnoredActor(Chase);
		if (!Net || !GetWorld()->SweepTestByChannel(V->GetActorLocation(), Chase->GetActorLocation(), FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(60.f), Clear))
		{
			Route.Reset();
			CheckDriveStuck(V);
			return;
		}
		DriveDest = Chase->GetActorLocation();
		if (Route.Num() && Net->FindNearestNode(DriveDest, 4500.f, Chase) != Route.Last())
		{
			Route.Reset();
		}
	}
	if (Route.Num() == 0)
	{
		TArray<FVector> Parked;   // other vehicles in the way (generator spots are road nodes)
		for (TActorIterator<AMFVehicle> It(GetWorld()); It; ++It)
		{
			if (*It != V && *It != V->GetTrailer())
			{
				Parked.Add(It->GetActorLocation());
			}
		}
		const int32 From = Net->FindNearestNode(V->GetActorLocation(), 4500.f, V);
		const int32 To = Net->FindNearestNode(DriveDest, 4500.f, DriveGoal.Get());
		if (From == INDEX_NONE || To == INDEX_NONE || !Net->FindRoute(From, To, Route, Parked))
		{
			LeaveVehicle(V, TEXT("no road"));
			return;
		}
		RouteStep = 0;
		if (ChaseTarget.IsValid() && Route.Num() <= 1)
		{
			Route.Reset();   // at the road's end already: straight at it
			CheckDriveStuck(V);
			return;
		}
		UE_LOG(LogMF, Verbose, TEXT("%s: driving %s, %d road nodes to %s"), *GetName(), *V->GetName(), Route.Num(), *DriveDest.ToCompactString());
	}
	// next node: reached, or already passed on the way to the one after it (that one closer
	// and in plain sight: not through the wall of a parking bay)
	const FVector Here = V->GetActorLocation();
	FCollisionQueryParams Sight(SCENE_QUERY_STAT(MFBotRoad), false, V);
	while (RouteStep < Route.Num())
	{
		const FVector N = Net->Nodes[Route[RouteStep]];
		const bool bPassed = RouteStep + 1 < Route.Num()
			&& FVector::Dist2D(Here, Net->Nodes[Route[RouteStep + 1]]) < FVector::Dist2D(N, Net->Nodes[Route[RouteStep + 1]])
			&& !GetWorld()->SweepTestByChannel(Here, Net->Nodes[Route[RouteStep + 1]] + FVector(0.f, 0.f, 50.f), FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(60.f), Sight);
		if (FVector::Dist2D(Here, N) > 450.f && !bPassed)
		{
			break;
		}
		++RouteStep;
		DriveStuckCount = FMath::Max(0, DriveStuckCount - 1);
	}
	if (RouteStep >= Route.Num() || FVector::Dist2D(Here, DriveDest) < 1500.f)
	{
		if (ChaseTarget.IsValid())
		{
			Route.Reset();   // nearly there: straight at it from here
			CheckDriveStuck(V);
			return;
		}
		LeaveVehicle(V, TEXT("arrived"));   // MoveDone: the rest on foot
		return;
	}
	UE_LOG(LogMF, VeryVerbose, TEXT("%s: %s at %s speed %.0f node %d/%d at %s%s"), *GetName(), *V->GetName(), *Here.ToCompactString(), V->GetVelocity().Size(),
		RouteStep, Route.Num(), *Net->Nodes[Route[RouteStep]].ToCompactString(), Now < ReverseUntil ? TEXT(" reversing") : TEXT(""));
	CheckDriveStuck(V);
}

void AMFBotController::CheckDriveStuck(AMFVehicle* V)
{
	// VehicleStuck: back off a moment and try again; after a few goes give the vehicle up
	// (bVehicleStuck: other bots leave it alone for a while)
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < ReverseUntil)
	{
		return;
	}
	if (V->GetVelocity().Size() > 150.f)
	{
		SlowSince = -1.f;
	}
	else if (SlowSince < 0.f)
	{
		SlowSince = Now;
	}
	else if (Now - SlowSince > 2.f)
	{
		SlowSince = -1.f;
		ReverseUntil = Now + 2.f;   // a few back-and-forths turn it out of a tight bay
		if (++DriveStuckCount >= 6)
		{
			V->BotStuckUntil = Now + 30.f;
			LeaveVehicle(V, TEXT("stuck"));
		}
	}
}

void AMFBotController::Steer(AMFVehicle* V)
{
	const AMFRoadNetwork* Net = AMFRoadNetwork::Get(GetWorld());
	const AActor* Chase = Route.Num() ? nullptr : ChaseTarget.Get();   // on a route: its nodes first
	if (!Chase && (!Net || !Route.IsValidIndex(RouteStep)))
	{
		V->SetDriveInput(0.f, 0.f, true);
		return;
	}
	const FVector Local = V->GetActorTransform().InverseTransformPositionNoScale(Chase ? Chase->GetActorLocation() : Net->Nodes[Route[RouteStep]]);
	const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));   // + : to the right
	if (GetWorld()->GetTimeSeconds() < ReverseUntil || (FMath::Abs(Angle) > 110.f && Local.Size2D() < 1500.f))
	{
		// backing up turns the nose the other way
		V->SetDriveInput(-1.f, -FMath::Sign(Angle), false);
		return;
	}
	// RoadPoint.MaxSpeed caps the speed (255 = flat out), slower into bends
	const float Limit = V->GetTopSpeed() * (Chase ? 1.f : Net->MaxSpeed[Route[RouteStep]] / 255.f);
	const float Want = FMath::Max(Limit * FMath::Lerp(1.f, 0.35f, FMath::Min(FMath::Abs(Angle) / 90.f, 1.f)), 500.f);
	const float Speed = FVector::DotProduct(V->GetVelocity(), V->GetActorForwardVector());
	V->SetDriveInput(Speed < Want ? 1.f : (Speed > Want * 1.25f ? -1.f : 0.f), FMath::Clamp(Angle / 30.f, -1.f, 1.f), false);
}

void AMFBotController::LeaveVehicle(AMFVehicle* V, const TCHAR* Why)
{
	UE_LOG(LogMF, Verbose, TEXT("%s: leaving %s at %s (%s, node %d / %d, %.0f s)"), *GetName(), *V->GetName(), *V->GetActorLocation().ToCompactString(),
		Why, RouteStep, Route.Num(), GetWorld()->GetTimeSeconds() - DriveStart);
	V->SetDriveInput(0.f, 0.f, true);   // out with the handbrake on
	Route.Reset();
	DontEnterVehicleTime = GetWorld()->GetTimeSeconds() + 1.5f;
	V->Exit(GetWorld()->GetTimeSeconds() - DriveStart > 200.f);   // boxed in: keep trying, at last squeeze out
}
