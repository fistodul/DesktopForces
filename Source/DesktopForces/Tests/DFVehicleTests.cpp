#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Core/DFContent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Particles/ParticleSystemComponent.h"
#include "Player/DFCharacter.h"
#include "Vehicles/Humvee/Humvee.h"

namespace {
	class FDFVehiclePhysicsCheck : public IAutomationLatentCommand {
	public:
		FDFVehiclePhysicsCheck(FAutomationTestBase* InTest, UWorld* InWorld)
			: Test(InTest),
			  World(InWorld) {}

		virtual bool Update() override {
			const double Now = World->GetTimeSeconds();
			if (!Car.IsValid()) {
				if (Stage != 0) {
					Test->AddError(TEXT("Test vehicle was destroyed unexpectedly."));
					Cleanup();
					return true;
				}
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
				Car = World->SpawnActor<AHumvee>(Origin + FVector(0, 0, 200), FRotator::ZeroRotator, Spawn);
				Car->Health = 50;
				Next = Now + 4;
				Stage = 1;
				return false;
			}
			if (Now < Next)
				return false;
			switch (Stage++) {
			case 1: {
				UParticleSystemComponent* Smoke = nullptr;
				// Pooled emitters belong to the world, not to the actor they attach to.
				for (USceneComponent* Child : Car->GetRootComponent()->GetAttachChildren())
					if (UParticleSystemComponent* Effect = Cast<UParticleSystemComponent>(Child))
						Smoke = Effect;
				Test->TestTrue(TEXT("Damaged vehicle emits smoke locally"), Smoke && Smoke->IsActive());
				if (Smoke) {
					float Rate = 0.f, Alpha = 0.f;
					Smoke->GetFloatParameter(TEXT("SmokeRate"), Rate);
					Smoke->GetFloatParameter(TEXT("SmokeAlpha"), Alpha);
					Test->TestTrue(TEXT("Damage smoke rate reaches the component"), Rate > 2.f);
					Test->TestTrue(TEXT("Damage smoke opacity reaches the component"), Alpha > 0.12f);
				}
				Car->BlowOutWheel(0);
				Next = Now + 5; // let the original blowout impulse settle
				break;
			}
			case 2:
				RestPosition = Car->GetActorLocation();
				Next = Now + 5;
				break;
			case 3: {
				const float Drift = FVector::Dist2D(RestPosition, Car->GetActorLocation());
				const float Speed = Car->GetBody()->GetPhysicsLinearVelocity().Size2D();
				Test->AddInfo(FString::Printf(TEXT("Flat tyre: %.2f cm drift over 5 s, %.2f cm/s horizontal speed"), Drift, Speed));
				Test->TestTrue(TEXT("A parked flat tyre does not cause sustained drift"), Drift < 10.f && Speed < 5.f);
				TInlineComponentArray<USphereComponent*> Spheres(Car.Get());
				int32 BlockingTyres = 0;
				for (USphereComponent* Sphere : Spheres)
					if (Sphere->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block) {
						++BlockingTyres;
						const FVector Centre = Sphere->GetComponentLocation();
						const FVector Out = Car->GetActorRightVector() * (Car->GetActorTransform().InverseTransformPosition(Centre).Y > 0 ? 1.f : -1.f);
						FHitResult Hit;
						Test->TestTrue(TEXT("Pawn query hits a tyre outside the chassis"), World->LineTraceSingleByChannel(Hit, Centre + Out * 200.f, Centre, ECC_Pawn));
						Test->TestEqual(TEXT("Tyre surface blocks the query"), Hit.GetComponent(), static_cast<UPrimitiveComponent*>(Sphere));
					}
				Test->TestEqual(TEXT("All Humvee tyres block pawn queries"), BlockingTyres, 4);
				Car->SetActorRotation(FRotator::ZeroRotator, ETeleportType::TeleportPhysics);
				Car->GetBody()->SetPhysicsLinearVelocity(FVector(2500, 0, 0));
				Car->GetBody()->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
				FActorSpawnParameters Spawn;
				Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Victim = World->SpawnActor<ADFCharacter>(FVector(Car->GetActorLocation().X + 350, Origin.Y, Origin.Z + 88), FRotator::ZeroRotator, Spawn);
				Victim->SetInLoadout(false);
				Next = Now + 0.2;
				break;
			}
			default:
				Test->TestTrue(TEXT("Fast vehicle runs over a stationary pawn"), Victim.IsValid() && !Victim->IsAlive());
				Test->AddInfo(FString::Printf(TEXT("Speed after pawn contact: %.2f cm/s"), Car->GetBody()->GetPhysicsLinearVelocity().X));
				Test->TestTrue(TEXT("Pawn does not stop the vehicle like a wall"), Car->GetBody()->GetPhysicsLinearVelocity().X > 1000.f);
				Cleanup();
				return true;
			}
			return false;
		}

	private:
		void Cleanup() {
			if (Victim.IsValid())
				Victim->Destroy();
			if (Car.IsValid())
				Car->Destroy();
			if (Floor.IsValid())
				Floor->Destroy();
		}
		FAutomationTestBase* Test;
		UWorld* World;
		TWeakObjectPtr<AActor> Floor;
		TWeakObjectPtr<AHumvee> Car;
		TWeakObjectPtr<ADFCharacter> Victim;
		FVector Origin = FVector(30000, 30000, 10000);
		FVector RestPosition;
		double Next = 0;
		int32 Stage = 0;
	};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFVehiclePhysicsTest, "DesktopForces.Gameplay.VehiclePhysics",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FDFVehiclePhysicsTest::RunTest(const FString& Parameters) {
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (UWorld* World = Context.World(); World && World->IsGameWorld() && World->GetAuthGameMode()) {
			ADD_LATENT_AUTOMATION_COMMAND(FDFVehiclePhysicsCheck(this, World));
			return true;
		}
	AddError(TEXT("Run this test in a standalone/listen game."));
	return false;
}

#endif
