#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/DFGameMode.h"
#include "Player/DFCharacter.h"
#include "Vehicles/Armadillo/Armadillo.h"
#include "Vehicles/Buggy/Buggy.h"
#include "Vehicles/Humvee/Humvee.h"
#include "Vehicles/Truck/Truck.h"

namespace {
	// A repeatable DF baseline, not a claimed match to MF's native physics. Keep it
	// separate from effect checks so it also runs with -nullrhi and fixed frame rate.
	class FDFVehicleHandlingCheck : public IAutomationLatentCommand {
	public:
		FDFVehicleHandlingCheck(FAutomationTestBase* InTest, UWorld* InWorld)
			: Test(InTest),
			  World(InWorld) {}

		virtual bool Update() override {
			const double Now = World->GetTimeSeconds();
			if (!Floor.IsValid()) {
				Floor = World->SpawnActor<AActor>();
				UBoxComponent* Ground = NewObject<UBoxComponent>(Floor.Get());
				Floor->SetRootComponent(Ground);
				Ground->InitBoxExtent(FVector(100000.f, 20000.f, 50.f));
				Ground->SetWorldLocation(Origin + FVector(50000.f, 0.f, -50.f));
				Ground->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Ground->SetCollisionObjectType(ECC_WorldStatic);
				Ground->SetCollisionResponseToAllChannels(ECR_Block);
				Ground->SetMobility(EComponentMobility::Static);
				Ground->RegisterComponent();
				FActorSpawnParameters Spawn;
				Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				for (UClass* Type : { ABuggy::StaticClass(), AHumvee::StaticClass(), ATruck::StaticClass(), AArmadillo::StaticClass() })
					for (int32 Nitro = 0; Nitro < 2; ++Nitro) {
						const FVector Position = Origin + FVector(0.f, Cars.Num() * 3000.f - 10500.f, 400.f);
						ADFVehicle* Car = World->SpawnActor<ADFVehicle>(Type, Position, FRotator::ZeroRotator, Spawn);
						Cars.Add(Car);
						ADFCharacter* DriverPawn = World->SpawnActor<ADFCharacter>(Position + FVector(0.f, 200.f, 0.f), FRotator::ZeroRotator, Spawn);
						Drivers.Add(DriverPawn);
						AAIController* Controller = World->SpawnActor<AAIController>();
						Controllers.Add(Controller);
						DriverPawn->SetInLoadout(false);
						Controller->Possess(DriverPawn);
						if (!Car->EnterSeat(DriverPawn, 0)) {
							Test->AddError(TEXT("Handling fixture could not enter the driver's seat."));
							Cleanup();
							return true;
						}
					}
				StartAt = Now + 4.f; // settle suspension before timing acceleration
				return false;
			}
			const double Elapsed = Now - StartAt;
			for (int32 Index = 0; Index < Cars.Num(); ++Index) {
				if (!Cars[Index].IsValid() || !Drivers[Index].IsValid() || Cars[Index]->GetDriver() != Drivers[Index].Get()) {
					Test->AddError(TEXT("Handling fixture lost a vehicle or its driver."));
					Cleanup();
					return true;
				}
				Cars[Index]->SetDriveInput(Elapsed >= 0.f ? 1.f : 0.f, Elapsed >= 10.f ? 1.f : 0.f, Elapsed < 0.f);
				Cars[Index]->SetNitro(Elapsed >= 0.f && Index % 2 == 1);
			}
			if (Elapsed < Sample)
				return false;
			for (int32 Index = 0; Index < Cars.Num(); ++Index) {
				ADFVehicle* Car = Cars[Index].Get();
				const FVector Velocity = Car->GetBody()->GetPhysicsLinearVelocity();
				const FRotator Rotation = Car->GetActorRotation();
				Test->AddInfo(FString::Printf(TEXT("Handling %s nitro=%d t=%.3f speed=%.3f m/s forward=%.3f m/s x=%.2f m yaw=%.2f roll=%.2f"),
					*Car->GetClass()->GetName(), Index % 2, Elapsed, Velocity.Size() / 100.f, FVector::DotProduct(Velocity, Car->GetActorForwardVector()) / 100.f,
					(Car->GetActorLocation().X - Origin.X) / 100.f, Rotation.Yaw, Rotation.Roll));
				if (Sample == 10) {
					Test->TestTrue(TEXT("Vehicle accelerates under sustained input"), Velocity.Size2D() > 500.f);
					Test->TestTrue(TEXT("Vehicle stays upright during straight acceleration"), Car->GetActorUpVector().Z > 0.8f);
				}
			}
			if (++Sample <= 12)
				return false;
			Cleanup();
			return true;
		}

	private:
		void Cleanup() {
			for (TWeakObjectPtr<ADFVehicle> Car : Cars)
				if (Car.IsValid())
					Car->Destroy();
			for (TWeakObjectPtr<ADFCharacter> DriverPawn : Drivers)
				if (DriverPawn.IsValid())
					DriverPawn->Destroy();
			for (TWeakObjectPtr<AAIController> Controller : Controllers)
				if (Controller.IsValid())
					Controller->Destroy();
			if (Floor.IsValid())
				Floor->Destroy();
		}
		FAutomationTestBase* Test;
		UWorld* World;
		TWeakObjectPtr<AActor> Floor;
		TArray<TWeakObjectPtr<ADFVehicle>> Cars;
		TArray<TWeakObjectPtr<ADFCharacter>> Drivers;
		TArray<TWeakObjectPtr<AAIController>> Controllers;
		const FVector Origin = FVector(-30000.f, 30000.f, 10000.f);
		double StartAt = 0.f;
		int32 Sample = 0;
	};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFVehicleHandlingTest, "DesktopForces.Gameplay.VehicleHandling",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FDFVehicleHandlingTest::RunTest(const FString& Parameters) {
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (UWorld* World = Context.World(); World && World->IsGameWorld()) {
			const ADFGameMode* Mode = World->GetAuthGameMode<ADFGameMode>();
			if (Mode && Mode->bTurboCars) {
				ADD_LATENT_AUTOMATION_COMMAND(FDFVehicleHandlingCheck(this, World));
				return true;
			}
		}
	AddError(TEXT("Run this test in a standalone/listen game with ?TurboCars=1."));
	return false;
}

#endif
