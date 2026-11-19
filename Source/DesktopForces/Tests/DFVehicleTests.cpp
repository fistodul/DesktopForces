#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/DFContent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Particles/ParticleSystemComponent.h"
#include "Player/DFCharacter.h"
#include "Core/DFTypes.h"
#include "Engine/DamageEvents.h"
#include "Vehicles/ArmedAPC/ArmedAPC.h"
#include "Vehicles/Buggy/Buggy.h"
#include "Vehicles/Humvee/Humvee.h"
#include "Vehicles/Jeep/Jeep.h"

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
				// The Humvee's bonnet descends towards the nose. A pawn box used to put
				// an invisible flat platform 68-87 cm above these converted mesh points.
				// Wait until this stage so all query bodies have entered the physics scene.
				const FTransform VehicleTransform = Car->GetActorTransform();
				for (float X : { 150.f, 200.f, 250.f }) {
					const FVector Start = VehicleTransform.TransformPosition(FVector(X, 0.f, 400.f) - Car->GetBoxCenter());
					const FVector End = VehicleTransform.TransformPosition(FVector(X, 0.f, -100.f) - Car->GetBoxCenter());
					FHitResult Weapon, Pawn;
					World->LineTraceSingleByChannel(Weapon, Start, End, ECC_GameTraceChannel2);
					World->LineTraceSingleByChannel(Pawn, Start, End, ECC_Pawn);
					Test->TestTrue(TEXT("Bonnet is queryable for weapons and pawns"), Weapon.GetActor() == Car.Get() && Pawn.GetActor() == Car.Get());
					Test->TestEqual(TEXT("Pawn stands on the visible bonnet"), Pawn.GetComponent(), Weapon.GetComponent());
					Test->TestTrue(TEXT("Pawn bonnet height matches the model"), FVector::Dist(Pawn.ImpactPoint, Weapon.ImpactPoint) < 1.f);
					// Character movement uses capsule sweeps, not line traces. Check both:
					// the tyres can be the first contact near the sides of the bonnet.
					const FCollisionShape Capsule = FCollisionShape::MakeCapsule(38.25f, 87.75f);
					World->SweepSingleByChannel(Weapon, Start, End, FQuat::Identity, ECC_GameTraceChannel2, Capsule);
					World->SweepSingleByChannel(Pawn, Start, End, FQuat::Identity, ECC_Pawn, Capsule);
					Test->TestTrue(TEXT("Character capsule reaches the visible vehicle"), Weapon.GetActor() == Car.Get() && Pawn.GetActor() == Car.Get());
					Test->TestTrue(TEXT("Pawn capsule has no invisible platform above the bonnet"), FVector::Dist(Pawn.Location, Weapon.Location) < 1.f);
				}
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

	/** An empty jeep on a 10 degree slope stands on its tyres, hull clear, and stays put. */
	class FDFVehicleParkingCheck : public IAutomationLatentCommand {
	public:
		FDFVehicleParkingCheck(FAutomationTestBase* InTest, UWorld* InWorld)
			: Test(InTest),
			  World(InWorld) {}

		virtual bool Update() override {
			const double Now = World->GetTimeSeconds();
			if (Stage == 0) {
				Floor = World->SpawnActor<AActor>();
				UBoxComponent* Box = NewObject<UBoxComponent>(Floor.Get());
				Floor->SetRootComponent(Box);
				Box->InitBoxExtent(FVector(3000, 3000, 50));
				Box->SetWorldLocationAndRotation(Origin - FVector(0, 0, 50), FRotator(10.f, 0.f, 0.f));
				Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Box->SetCollisionObjectType(ECC_WorldStatic);
				Box->SetCollisionResponseToAllChannels(ECR_Block);
				Box->SetMobility(EComponentMobility::Static);
				Box->RegisterComponent();
				FActorSpawnParameters Spawn;
				Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Car = World->SpawnActor<AJeep>(Origin + FVector(0, 0, 250), FRotator(10.f, 0.f, 0.f), Spawn);
				Next = Now + 6;
				Stage = 1;
				return false;
			}
			if (!Car.IsValid()) {
				Test->AddError(TEXT("Parked jeep was destroyed unexpectedly."));
				Cleanup();
				return true;
			}
			if (Now < Next)
				return false;
			if (Stage == 1) {
				// the hull's underside above the slope, under the middle of the car
				FCollisionQueryParams Params(SCENE_QUERY_STAT(DFParkTest), false, Car.Get());
				FHitResult Ground, Hull;
				const FVector Middle = Car->GetActorLocation();
				World->LineTraceSingleByChannel(Ground, Middle, Middle - FVector(0, 0, 1000), ECC_Visibility, Params);
				World->LineTraceSingleByChannel(Hull, Ground.ImpactPoint + FVector(0, 0, 1), Middle + FVector(0, 0, 300), ECC_GameTraceChannel2);
				const float Clearance = Hull.bBlockingHit ? Hull.Distance + 1.f : -1.f;
				Test->AddInfo(FString::Printf(TEXT("Jeep hull clearance on the slope: %.1f cm"), Clearance));
				Test->TestTrue(TEXT("Jeep stands on its tyres, hull clear of the ground"), Ground.bBlockingHit && Clearance > 25.f && Clearance < 60.f);
				Rest = Car->GetActorLocation();
				Next = Now + 6;
				Stage = 2;
				return false;
			}
			const float Drift = FVector::Dist(Rest, Car->GetActorLocation());
			Test->AddInfo(FString::Printf(TEXT("Parked jeep on a 10 degree slope: %.2f cm drift over 6 s"), Drift));
			Test->TestTrue(TEXT("A parked car holds on a slope"), Drift < 2.f);
			Test->TestFalse(TEXT("A parked car sleeps"), Car->GetBody()->RigidBodyIsAwake());
			Cleanup();
			return true;
		}

	private:
		void Cleanup() {
			if (Car.IsValid())
				Car->Destroy();
			if (Floor.IsValid())
				Floor->Destroy();
		}
		FAutomationTestBase* Test;
		UWorld* World;
		TWeakObjectPtr<AActor> Floor;
		TWeakObjectPtr<AJeep> Car;
		FVector Origin = FVector(-30000, 30000, 10000);
		FVector Rest;
		double Next = 0;
		int32 Stage = 0;
	};
	/** MF Vehicle.BlowUp next to an APC: HurtRadius 300 x 1.5 (explosive) x (1 - 0.84 armour) takes
	 *  up to 72 of its 100. And an expiring vehicle flashes on every mesh, turret and axles too. */
	class FDFVehicleBlastCheck : public IAutomationLatentCommand {
	public:
		FDFVehicleBlastCheck(FAutomationTestBase* InTest, UWorld* InWorld)
			: Test(InTest),
			  World(InWorld) {}

		virtual bool Update() override {
			const double Now = World->GetTimeSeconds();
			if (Stage == 0) {
				Floor = World->SpawnActor<AActor>();
				UBoxComponent* Box = NewObject<UBoxComponent>(Floor.Get());
				Floor->SetRootComponent(Box);
				Box->InitBoxExtent(FVector(3000, 3000, 50));
				Box->SetWorldLocation(Origin - FVector(0, 0, 50));
				Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Box->SetCollisionObjectType(ECC_WorldStatic);
				Box->SetCollisionResponseToAllChannels(ECR_Block);
				Box->SetMobility(EComponentMobility::Static);
				Box->RegisterComponent();
				FActorSpawnParameters Spawn;
				Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Buggy = World->SpawnActor<ABuggy>(Origin + FVector(0, 0, 150), FRotator::ZeroRotator, Spawn);
				Apc = World->SpawnActor<AArmedAPC>(Origin + FVector(0, 450, 200), FRotator::ZeroRotator, Spawn);
				Next = Now + 3; // settle on the floor
				Stage = 1;
				return false;
			}
			if (Now < Next)
				return false;
			if (!Apc.IsValid() || (Stage == 1 && !Buggy.IsValid())) { // the buggy goes once it blows
				Test->AddError(TEXT("Test vehicles were destroyed unexpectedly."));
				Cleanup();
				return true;
			}
			if (Stage == 1) {
				if (FFloatProperty* Prop = FindFProperty<FFloatProperty>(ADFVehicle::StaticClass(), TEXT("ExpireAt")))
					Prop->SetPropertyValue_InContainer(Apc.Get(), float(Now) + 20.f);
				const FDamageEvent Blast(UDFDamage_Explosion::StaticClass());
				Buggy->TakeDamage(10000.f, Blast, nullptr, nullptr);
				Next = Now + 0.5;
				Stage = 2;
				return false;
			}
			const int32 Taken = 100 - Apc->Health;
			Test->AddInfo(FString::Printf(TEXT("APC beside an exploding buggy took %d (MF: up to 72)"), Taken));
			Test->TestTrue(TEXT("The blast does MF's 300 x 1.5 x armour, not half of it"), Taken > 36 && Taken <= 72);
			TInlineComponentArray<UStaticMeshComponent*> Meshes(Apc.Get());
			int32 Flashing = 0;
			for (UStaticMeshComponent* Mesh : Meshes)
				Flashing += Mesh->GetOverlayMaterial() != nullptr;
			Test->AddInfo(FString::Printf(TEXT("Expiring APC: %d of %d meshes flash"), Flashing, Meshes.Num()));
			Test->TestTrue(TEXT("Body, wheels, axles, suspension and turret all flash"), Meshes.Num() > 5 && Flashing == Meshes.Num());
			Cleanup();
			return true;
		}

	private:
		void Cleanup() {
			for (TWeakObjectPtr<AActor> A : { TWeakObjectPtr<AActor>(Buggy.Get()), TWeakObjectPtr<AActor>(Apc.Get()), Floor })
				if (A.IsValid())
					A->Destroy();
		}
		FAutomationTestBase* Test;
		UWorld* World;
		TWeakObjectPtr<AActor> Floor;
		TWeakObjectPtr<ABuggy> Buggy;
		TWeakObjectPtr<AArmedAPC> Apc;
		FVector Origin = FVector(-30000, 30000, 10000);
		double Next = 0;
		int32 Stage = 0;
	};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFVehicleParkingTest, "DesktopForces.Gameplay.VehicleParking",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FDFVehicleParkingTest::RunTest(const FString& Parameters) {
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (UWorld* World = Context.World(); World && World->IsGameWorld() && World->GetAuthGameMode()) {
			ADD_LATENT_AUTOMATION_COMMAND(FDFVehicleParkingCheck(this, World));
			return true;
		}
	AddError(TEXT("Run this test in a standalone/listen game."));
	return false;
}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFVehicleBlastTest, "DesktopForces.Gameplay.VehicleBlast",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FDFVehicleBlastTest::RunTest(const FString& Parameters) {
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (UWorld* World = Context.World(); World && World->IsGameWorld() && World->GetAuthGameMode()) {
			ADD_LATENT_AUTOMATION_COMMAND(FDFVehicleBlastCheck(this, World));
			return true;
		}
	AddError(TEXT("Run this test in a standalone/listen game."));
	return false;
}

#endif
