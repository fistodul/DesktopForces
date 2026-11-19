#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Player/DFCharacter.h"
#include "Player/DFCharacterMovement.h"
#include "World/DFLadder.h"

namespace {
	class FDFLadderRungsCheck : public IAutomationLatentCommand {
	public:
		FDFLadderRungsCheck(FAutomationTestBase* InTest, UWorld* InWorld)
			: Test(InTest),
			  World(InWorld) {}

		virtual bool Update() override {
			const double Now = World->GetTimeSeconds();
			if (!Soldier.IsValid()) {
				if (Stage != 0) {
					Test->AddError(TEXT("Ladder test soldier was destroyed."));
					Cleanup();
					return true;
				}
				Geometry = World->SpawnActor<AActor>();
				auto Box = [this](const FVector& At, const FVector& Extent) {
					UBoxComponent* Part = NewObject<UBoxComponent>(Geometry.Get());
					if (!Geometry->GetRootComponent())
						Geometry->SetRootComponent(Part);
					else
						Part->SetupAttachment(Geometry->GetRootComponent());
					Part->InitBoxExtent(Extent);
					Part->SetWorldLocation(Origin + At);
					Part->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
					Part->SetCollisionObjectType(ECC_WorldStatic);
					Part->SetCollisionResponseToAllChannels(ECR_Block);
					Part->SetMobility(EComponentMobility::Static);
					Part->RegisterComponent();
				};
				Box(FVector(0, 0, -50), FVector(500, 500, 50));
				// Thin solid rungs in front of a wall, like converted BSP ladders. A cylinder can
				// climb beside these; pushing a rounded capsule into them wedges its top underneath.
				Box(FVector(70, 0, 350), FVector(20, 120, 350));
				for (float Z = 60; Z < 700; Z += 60)
					Box(FVector(20, 0, Z), FVector(8, 70, 5));
				FActorSpawnParameters Spawn;
				Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Ladder = World->SpawnActor<ADFLadderVolume>(Origin + FVector(0, 0, 350), FRotator::ZeroRotator, Spawn);
				Soldier = World->SpawnActor<ADFCharacter>(Origin + FVector(-100, 0, 100), FRotator::ZeroRotator, Spawn);
				Soldier->SetInLoadout(false);
				Soldier->GetMFMovement()->bRunPhysicsWithNoController = true;
				// Spawned without a controller: initialize walking explicitly, as possession would.
				Soldier->GetMFMovement()->SetMovementMode(MOVE_Walking);
				Next = Now + 0.25;
				Stage = 1;
				return false;
			}
			if (Stage == 1) {
				if (Now < Next)
					return false; // registration must have reached the physics scene
				StartZ = Soldier->GetActorLocation().Z;
				LowestZ = StartZ;
				Next = Now + 1.0;
				Stage = 2;
			}
			if (Stage == 3) {
				Soldier->AddMovementInput(-FVector::ForwardVector, 1.f, true);
				if (Now < Next)
					return false;
				Test->TestTrue(TEXT("Backward descends the same ladder"), DescentZ - Soldier->GetActorLocation().Z > 150.f);
				Test->TestTrue(TEXT("Ascent and descent keep the same stand-off"), FMath::Abs(Soldier->GetActorLocation().X - DescentX) < 3.f);
				Cleanup();
				return true;
			}
			Soldier->AddMovementInput(FVector::ForwardVector, 1.f, true);
			LowestZ = FMath::Min(LowestZ, Soldier->GetActorLocation().Z);
			if (Now < Next)
				return false;
			const float Rise = Soldier->GetActorLocation().Z - StartZ;
			Test->AddInfo(FString::Printf(TEXT("Ladder forward for 1 s: rise %.1f cm, drop %.1f cm, movement %s"), Rise, StartZ - LowestZ, *Soldier->GetMFMovement()->GetMovementName()));
			Test->TestTrue(TEXT("Forward climbs past multiple solid rungs"), Rise > 300.f);
			Test->TestTrue(TEXT("Forward does not pull the soldier down"), LowestZ >= StartZ - 2.f);
			Test->TestTrue(TEXT("Approaching the ladder does not retain an excessive gap"), FMath::Abs(Soldier->GetActorLocation().X - Origin.X) < 70.f);
			DescentX = Soldier->GetActorLocation().X;
			DescentZ = Soldier->GetActorLocation().Z;
			Next = Now + 0.5;
			Stage = 3;
			return false;
		}

	private:
		void Cleanup() {
			if (Soldier.IsValid())
				Soldier->Destroy();
			if (Ladder.IsValid())
				Ladder->Destroy();
			if (Geometry.IsValid())
				Geometry->Destroy();
		}
		FAutomationTestBase* Test;
		UWorld* World;
		TWeakObjectPtr<AActor> Geometry;
		TWeakObjectPtr<ADFLadderVolume> Ladder;
		TWeakObjectPtr<ADFCharacter> Soldier;
		const FVector Origin = FVector(-30000, -30000, 10000);
		double Next = 0;
		float StartZ = 0;
		float LowestZ = 0;
		float DescentX = 0, DescentZ = 0;
		int32 Stage = 0;
	};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFLadderRungsTest, "DesktopForces.Movement.LadderRungs",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FDFLadderRungsTest::RunTest(const FString& Parameters) {
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (Context.World() && Context.World()->IsGameWorld()) {
			ADD_LATENT_AUTOMATION_COMMAND(FDFLadderRungsCheck(this, Context.World()));
			return true;
		}
	AddError(TEXT("Run this test in a game world."));
	return false;
}

#endif
