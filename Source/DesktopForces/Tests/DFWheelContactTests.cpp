#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Vehicles/Humvee/Humvee.h"

// Deferred queries: newly registered fixture bodies need a physics update first.
class FDFWheelContactCheck : public IAutomationLatentCommand {
public:
	FDFWheelContactCheck(FAutomationTestBase* InTest, UWorld* InWorld)
		: Test(InTest),
		  World(InWorld) {}
	virtual bool Update() override {
		if (!Car.IsValid()) {
			if (bCreated) {
				Test->AddError(TEXT("Wheel fixture vehicle disappeared."));
				Cleanup();
				return true;
			}
			bCreated = true;
			const FVector Origin(-60000.f, -60000.f, 10000.f);
			auto Box = [&](const FVector& At, const FVector& Extent, const FRotator& Rotation = FRotator::ZeroRotator) {
				AActor* Actor = World->SpawnActor<AActor>();
				UBoxComponent* Part = NewObject<UBoxComponent>(Actor);
				Actor->SetRootComponent(Part);
				Part->InitBoxExtent(Extent);
				Part->SetWorldLocation(Origin + At);
				Part->SetWorldRotation(Rotation);
				Part->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Part->SetCollisionObjectType(ECC_WorldStatic);
				Part->SetCollisionResponseToAllChannels(ECR_Block);
				Part->SetMobility(EComponentMobility::Static);
				Part->RegisterComponent();
				return Actor;
			};
			Floor = Box(FVector(0, 0, -50), FVector(3000, 3000, 50));
			Curb = Box(FVector(500, 0, 20), FVector(500, 500, 20)); // x >= 0, top at 40 cm
			Wall = Box(FVector(500, 2000, 500), FVector(500, 500, 500));
			Ramp = Box(FVector(-1800, -1800, 200), FVector(500, 300, 20), FRotator(30, 0, 0));
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Car = World->SpawnActor<AHumvee>(Origin, FRotator::ZeroRotator, Spawn);
			Car->Body->SetSimulatePhysics(false);
			const FDFWheelSetup& Wheel = Car->Wheels[0];
			// Tyre reaches the edge while its axle is still 20 cm before it. A vertical
			// centre ray incorrectly reports only the lower road surface here.
			const FVector WheelCenter(-20, 0, Wheel.Radius + Car->SuspensionTravel * 0.5f);
			Car->SetActorLocation(Origin + WheelCenter - (Wheel.Position - Car->BoxCenter));
			ReadyAt = World->GetTimeSeconds() + 0.25;
			return false;
		}
		if (World->GetTimeSeconds() < ReadyAt)
			return false;
		FHitResult Hit;
		float Compression = 0.f;
		if (Test->TestTrue(TEXT("Tyre detects support before axle crosses curb"), Car->TraceWheel(0, Hit, Compression))) {
			Test->TestTrue(TEXT("Tyre contacts curb rather than lower road"), Hit.GetActor() == Curb.Get());
			Test->TestTrue(TEXT("Curb contact compresses suspension"), Compression > 10.f);
			Test->TestTrue(TEXT("Curb edge gives an upward radial normal"), Hit.Normal.Z > 0.1f);
		}
		const FDFWheelSetup& Wheel = Car->Wheels[0];
		const float ExpectedCompression = Car->SuspensionTravel * 0.25f;
		auto OnRoad = [&](float X, float Y, float Radius) {
			const FVector Center(X, Y, Radius + Car->SuspensionTravel * 0.5f - ExpectedCompression);
			Car->SetActorLocation(FVector(-60000.f, -60000.f, 10000.f) + Center - (Wheel.Position - Car->BoxCenter));
		};
		auto CheckRoad = [&](const TCHAR* Label) {
			if (Test->TestTrue(Label, Car->TraceWheel(0, Hit, Compression))) {
				Test->TestTrue(TEXT("Road supplies support"), Hit.GetActor() == Floor.Get());
				Test->TestTrue(TEXT("Flat-road compression retains the suspension geometry"), FMath::IsNearlyEqual(Compression, ExpectedCompression, 0.1f));
				Test->TestTrue(TEXT("Road support stays vertical"), Hit.Normal.Z > 0.99f);
			}
		};
		OnRoad(-500.f, 0.f, Wheel.Radius);
		CheckRoad(TEXT("Tyre finds a flat road"));
		OnRoad(-20.f, 2000.f, Wheel.Radius);
		CheckRoad(TEXT("Side-wall overlap still finds the road below"));
		Car->BlownWheels |= 1;
		OnRoad(-500.f, 0.f, Wheel.Radius * 0.75f);
		CheckRoad(TEXT("Deflated tyre supports its reduced ride height"));
		Car->BlownWheels = 0;
		// Measure spring travel from two actual sphere sweeps on a 30-degree ramp.
		// Horizontal chassis travel compresses the suspension even with no vertical
		// body velocity; damping must see that rate without a previous-frame sample.
		const FVector RampNormal = Ramp->GetActorUpVector();
		const FVector RampCenter = Ramp->GetActorLocation() + FVector(0.f, 0.f, (20.f + Wheel.Radius) / RampNormal.Z + Car->SuspensionTravel * 0.5f - ExpectedCompression);
		Car->SetActorLocation(RampCenter - (Wheel.Position - Car->BoxCenter));
		if (Test->TestTrue(TEXT("Tyre finds inclined support"), Car->TraceWheel(0, Hit, Compression))) {
			Test->TestTrue(TEXT("Inclined support belongs to the ramp"), Hit.GetActor() == Ramp.Get());
			const float Before = Compression;
			const FVector Velocity(100.f, 0.f, 0.f);
			const float Rate = Car->WheelCompressionRate(-Velocity, Hit.Normal, FVector::UpVector);
			constexpr float SampleTime = 0.01f;
			Car->SetActorLocation(Car->GetActorLocation() + Velocity * SampleTime);
			if (Test->TestTrue(TEXT("Tyre retains ramp support after horizontal travel"), Car->TraceWheel(0, Hit, Compression))) {
				const float MeasuredRate = (Compression - Before) / SampleTime;
				Test->TestTrue(TEXT("Uphill horizontal motion compresses the spring"), MeasuredRate > 50.f);
				Test->TestTrue(TEXT("Damper rate matches measured inclined spring travel"), FMath::IsNearlyEqual(Rate, MeasuredRate, 1.f));
			}
		}
		Test->TestEqual(TEXT("Stationary contact introduces no damping kick"), Car->WheelCompressionRate(FVector::ZeroVector, RampNormal, FVector::UpVector), 0.f);
		Test->TestEqual(TEXT("Flat-ground fall keeps its vertical damping rate"), Car->WheelCompressionRate(FVector(0.f, 0.f, 200.f), FVector::UpVector, FVector::UpVector), 200.f);
		Test->TestEqual(TEXT("Unsupported vertical face cannot divide by zero"), Car->WheelCompressionRate(FVector(100.f, 0.f, 0.f), FVector::ForwardVector, FVector::UpVector), 0.f);
		Cleanup();
		return true;
	}

private:
	void Cleanup() {
		if (Car.IsValid())
			Car->Destroy();
		if (Curb.IsValid())
			Curb->Destroy();
		if (Floor.IsValid())
			Floor->Destroy();
		if (Wall.IsValid())
			Wall->Destroy();
		if (Ramp.IsValid())
			Ramp->Destroy();
	}
	FAutomationTestBase* Test;
	UWorld* World;
	TWeakObjectPtr<AHumvee> Car;
	TWeakObjectPtr<AActor> Floor, Curb, Wall, Ramp;
	double ReadyAt = 0;
	bool bCreated = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFWheelContactTest, "DesktopForces.Gameplay.WheelCurbContact", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FDFWheelContactTest::RunTest(const FString& Parameters) {
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (Context.World() && Context.World()->IsGameWorld()) {
			ADD_LATENT_AUTOMATION_COMMAND(FDFWheelContactCheck(this, Context.World()));
			return true;
		}
	AddError(TEXT("Run wheel contact test in a game world."));
	return false;
}
#endif
