#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Vehicles/Buggy/Buggy.h"
#include "Weapons/SentryGun/SentryGun.h"
#include "Weapons/TripBombs/TripBombs.h"
#include "Player/DFCharacter.h"
#include "GameFramework/PlayerController.h"

namespace {
	/** SentryGunTurret.CheckVehicleRunOver: a vehicle at KillSpeed runs a sentry turret down;
	 *  a slow one goes past it and leaves it standing. */
	class FDFSentryRunOverCheck : public IAutomationLatentCommand {
	public:
		FDFSentryRunOverCheck(FAutomationTestBase* InTest, UWorld* InWorld)
			: Test(InTest),
			  World(InWorld) {}

		virtual bool Update() override {
			const double Now = World->GetTimeSeconds();
			if (Stage == 0) {
				Floor = World->SpawnActor<AActor>();
				UBoxComponent* Box = NewObject<UBoxComponent>(Floor.Get());
				Floor->SetRootComponent(Box);
				Box->InitBoxExtent(FVector(5000, 2000, 50));
				Box->SetWorldLocation(Origin - FVector(0, 0, 50));
				Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Box->SetCollisionObjectType(ECC_WorldStatic);
				Box->SetCollisionResponseToAllChannels(ECR_Block);
				Box->SetMobility(EComponentMobility::Static);
				Box->RegisterComponent();
				FActorSpawnParameters Spawn;
				Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				// two lanes: a fast car at one turret, a slow one at the other
				for (int32 i = 0; i < 2; ++i) {
					const FVector Lane = Origin + FVector(0.f, i * 1500.f, 0.f);
					Turrets[i] = World->SpawnActor<ASentryTurret>(Lane + FVector(0.f, 0.f, 55.f), FRotator::ZeroRotator, Spawn);
					Cars[i] = World->SpawnActor<ABuggy>(Lane + FVector(-900.f, 0.f, 150.f), FRotator::ZeroRotator, Spawn);
				}
				if (!Turrets[0].IsValid() || !Turrets[1].IsValid() || !Cars[0].IsValid() || !Cars[1].IsValid()) {
					Test->AddError(TEXT("Could not spawn the turrets and buggies."));
					Cleanup();
					return true;
				}
				Next = Now + 3.0; // settle on the floor
				Stage = 1;
				return false;
			}
			if (Now < Next)
				return false;
			if (Stage == 1) {
				const float Speeds[2] = { 1800.f, 250.f }; // 18 m/s, over KillSpeed (8); 2.5 m/s
				for (int32 i = 0; i < 2; ++i)
					if (UPrimitiveComponent* Body = Cars[i].IsValid() ? Cast<UPrimitiveComponent>(Cars[i]->GetRootComponent()) : nullptr)
						Body->SetPhysicsLinearVelocity(FVector(Speeds[i], 0.f, 0.f));
				Next = Now + 2.5;
				Stage = 2;
				return false;
			}
			Test->TestTrue(TEXT("A vehicle at KillSpeed runs the turret down"), !Turrets[0].IsValid() || Turrets[0]->IsDead() || Turrets[0]->IsActorBeingDestroyed());
			Test->TestTrue(TEXT("A slow vehicle leaves the turret standing"), Turrets[1].IsValid() && !Turrets[1]->IsDead());
			Cleanup();
			return true;
		}

	private:
		void Cleanup() {
			for (int32 i = 0; i < 2; ++i) {
				if (Turrets[i].IsValid())
					Turrets[i]->Destroy();
				if (Cars[i].IsValid())
					Cars[i]->Destroy();
			}
			if (Floor.IsValid())
				Floor->Destroy();
		}

		FAutomationTestBase* Test;
		UWorld* World;
		FVector Origin = FVector(30000, -30000, 10000);
		TWeakObjectPtr<AActor> Floor;
		TWeakObjectPtr<ASentryTurret> Turrets[2];
		TWeakObjectPtr<ABuggy> Cars[2];
		int32 Stage = 0;
		double Next = 0.0;
	};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFSentryRunOverTest, "DesktopForces.Gameplay.SentryRunOver",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FDFSentryRunOverTest::RunTest(const FString& Parameters) {
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (UWorld* World = Context.World(); World && World->IsGameWorld()) {
			ADD_LATENT_AUTOMATION_COMMAND(FDFSentryRunOverCheck(this, World));
			return true;
		}
	AddError(TEXT("No game world."));
	return false;
}

// The throw gauge: 0 while the arm goes back, then rising steadily (5 a second for trip bombs)
// to 10; never starting at 3 and dropping back (the release bonus belongs to the release only).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFThrowGaugeTest, "DesktopForces.Gameplay.ThrowGauge", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FDFThrowGaugeTest::RunTest(const FString& Parameters) {
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (Context.World() && Context.World()->IsGameWorld() && Context.World()->GetFirstPlayerController())
			World = Context.World();
	ADFCharacter* Soldier = World ? Cast<ADFCharacter>(World->GetFirstPlayerController()->GetPawn()) : nullptr;
	if (!Soldier) {
		AddError(TEXT("Needs a soldier."));
		return false;
	}
	ATripBombs* Bombs = World->SpawnActor<ATripBombs>();
	Bombs->OnGiven(Soldier);
	Bombs->OnEquip();
	struct FState {
		TArray<int32> Levels;
		double Start = 0.0;
	};
	TSharedRef<FState> S = MakeShared<FState>();
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>([this, World, Bombs, S]() {
		const double Now = World->GetTimeSeconds();
		if (S->Start == 0.0) {
			if (Bombs->GetState() == EDFWeaponState::Idle) {
				S->Start = Now;
				Bombs->StartFire(true);
			}
			return false;
		}
		S->Levels.Add(Bombs->GetPowerLevel());
		if (Now - S->Start < 3.0)
			return false;
		Bombs->StopFire(true);
		bool bRising = true;
		for (int32 i = 1; i < S->Levels.Num(); ++i)
			bRising &= S->Levels[i] >= S->Levels[i - 1];
		TestEqual(TEXT("Gauge empty as the throw begins"), S->Levels[0], 0);
		TestTrue(TEXT("Gauge only rises"), bRising);
		TestEqual(TEXT("Gauge full after 3 s"), S->Levels.Last(), 10);
		Bombs->Destroy();
		return true;
	}));
	return true;
}

#endif
