#include "AI/MFBotController.h"
#include "Player/MFCharacter.h"
#include "Game/MFPlayerState.h"
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

const TArray<FMFBotProfile>& AMFBotController::Roster()
{
	// Rage/Classes/RageBotA..P: BotName, aFavouriteLoadout, PreferedTeam (0 blue / 1 red)
	static TArray<FMFBotProfile> R;
	if (R.Num() == 0)
	{
		auto Add = [](const TCHAR* Name, EMFTeam Team, std::initializer_list<TSubclassOf<AMFWeapon>> Items)
		{
			FMFBotProfile P;
			P.Name = Name;
			P.PreferredTeam = Team;
			P.Loadout = Items;
			R.Add(P);
		};
		using B = EMFTeam;
		const TSubclassOf<AMFWeapon> MG = AMFWeapon_MachineGun::StaticClass(), Gren = AMFWeapon_Grenades::StaticClass(),
			Arm = AMFWeapon_Armour::StaticClass(), Rifle = AMFWeapon_Rifle::StaticClass(), HMG = AMFWeapon_HeavyMachineGun::StaticClass(),
			SG = AMFWeapon_Shotgun::StaticClass(), Trip = AMFWeapon_TripBombs::StaticClass(), RL = AMFWeapon_RocketLauncher::StaticClass(),
			Pistol = AMFWeapon_Pistol::StaticClass();
		Add(TEXT("Heywood"), B::Blue, { MG, Gren, Arm });
		Add(TEXT("Coffey"), B::Red, { MG, Gren, Arm });
		Add(TEXT("Jackson"), B::Blue, { Rifle, Arm });
		Add(TEXT("Dillon"), B::Red, { Rifle, Arm });
		Add(TEXT("Lydecker"), B::Blue, { HMG, Arm });
		Add(TEXT("Carter"), B::Red, { HMG, Arm });
		Add(TEXT("Gorman"), B::Blue, { SG, Trip, Arm });
		Add(TEXT("Powell"), B::Red, { SG, Trip, Arm });
		Add(TEXT("Sanchez"), B::Blue, { RL, Arm });
		Add(TEXT("Bennings"), B::Red, { RL, Arm });
		Add(TEXT("Bowman"), B::Blue, { SG, Trip, Trip, Arm });
		Add(TEXT("Wychek"), B::Red, { SG, Trip, Trip, Arm });
		Add(TEXT("Scagnetti"), B::Blue, { Pistol, Gren, Gren, Gren });
		Add(TEXT("Kurtz"), B::Red, { Pistol, Gren, Gren, Gren });
		Add(TEXT("Harris"), B::Blue, { SG, Arm });
		Add(TEXT("Palmer"), B::Red, { SG, Arm });
	}
	return R;
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
	for (const TSubclassOf<AMFWeapon>& Item : Profile.Loadout)
	{
		C->GiveWeapon(Item, false);
	}
	C->SetInLoadout(false);
	C->SwitchToBestWeapon();
}

AMFCharacter* AMFBotController::FindEnemy(AMFCharacter* C) const
{
	const bool bTeams = C->GetTeam() != EMFTeam::None;
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

void AMFBotController::Roam(AMFCharacter* C)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (GetMoveStatus() == EPathFollowingStatus::Moving && Now < NextRoam)
	{
		return;
	}
	NextRoam = Now + 15.f;
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation Out;
		if (Nav->GetRandomReachablePointInRadius(C->GetActorLocation(), 4500.f, Out))
		{
			MoveToLocation(Out.Location, 100.f);
		}
	}
}
