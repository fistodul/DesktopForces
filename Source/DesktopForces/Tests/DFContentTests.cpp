#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/DFGameMode.h"
#include "Player/DFCharacter.h"
#include "PhysicsEngine/BodySetup.h"
#include "Vehicles/Humvee/Humvee.h"
#include "Vehicles/Truck/Truck.h"

namespace {
	class FDFContentCheck : public IAutomationLatentCommand {
	public:
		FDFContentCheck(FAutomationTestBase* InTest, UWorld* InWorld)
			: Test(InTest),
			  World(InWorld) {}

		bool Update() override {
			const float Now = World->GetTimeSeconds();
			if (Stage == 2)
				for (ATruck* Truck : Trucks)
					Truck->SetDriveInput(1.f, 0.f, false);
			if (Car) {
				const FQuat Rotation = Car->GetActorQuat();
				MaxStep = FMath::Max(MaxStep, FMath::RadiansToDegrees(Previous.AngularDistance(Rotation)));
				Previous = Rotation;
			}
			if (Now < Next)
				return false;
			if (Stage++ == 0) {
				AActor* Floor = Spawn<AActor>(Origin - FVector(0, 0, 50));
				UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
				Floor->SetRootComponent(Box);
				Box->InitBoxExtent(FVector(5000, 5000, 50));
				Box->SetWorldLocation(Origin - FVector(0, 0, 50));
				Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Box->SetCollisionResponseToAllChannels(ECR_Block);
				Box->SetMobility(EComponentMobility::Static);
				Box->RegisterComponent();
				Car = Spawn<AHumvee>(Origin + FVector(0, 0, 240));
				Car->SetActorRotation(FRotator(0, 0, 100), ETeleportType::TeleportPhysics);
				Previous = Car->GetActorQuat();
				// Compare identical trucks, with turbo requested on only one of them.
				ADFGameMode* GM = World->GetAuthGameMode<ADFGameMode>();
				const bool bTurbo = GM->bTurboCars;
				GM->bTurboCars = true;
				for (int32 I = 0; I < 2; ++I) {
					Trucks[I] = Spawn<ATruck>(Origin + FVector(0, 1500 + I * 1500, 350));
					ADFCharacter* Driver = Spawn<ADFCharacter>(Origin + FVector(0, 1500 + I * 1500, 500));
					AAIController* Controller = Spawn<AAIController>(Origin);
					Controller->Possess(Driver);
					Driver->SetInLoadout(false);
					Trucks[I]->EnterSeat(Driver, 0);
				}
				GM->bTurboCars = bTurbo;
				Next = Now + 3;
				CheckGrating();
				return false;
			}
			if (Stage == 2) {
				if (Grating)
					CheckGratingQueries();
				Trucks[1]->SetNitro(true);
				Next = Now + 2;
				return false;
			}
			if (Stage == 3) {
				const float Gain = Trucks[1]->GetVelocity().X - Trucks[0]->GetVelocity().X;
				Test->AddInfo(FString::Printf(TEXT("Truck turbo speed gain: %.1f cm/s"), Gain));
				Test->TestTrue(TEXT("Truck turbo adds measurable acceleration"), Gain > 100.f);
				Trucks[1]->SetNitro(false);
				Next = Now + 12;
				return false;
			}
			Test->AddInfo(FString::Printf(TEXT("Recovery: up.Z %.3f, maximum rotation step %.1f degrees"), Car->GetActorUpVector().Z, MaxStep));
			Test->TestTrue(TEXT("Sideways vehicle recovers upright"), Car->GetActorUpVector().Z > 0.8f);
			Test->TestTrue(TEXT("Recovery rotates continuously instead of snapping"), MaxStep < 60.f);
			for (AActor* Actor : Actors)
				if (IsValid(Actor))
					Actor->Destroy();
			return true;
		}

	private:
		template <class T>
		T* Spawn(const FVector& Location) {
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			T* Actor = World->SpawnActor<T>(Location, FRotator::ZeroRotator, Params);
			Actors.Add(Actor);
			return Actor;
		}
		void CheckGrating() {
			UStaticMesh* Asset = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Maps/mf-Space_station_Data/Meshes/Pomost.Pomost"), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Asset) {
				Test->AddWarning(TEXT("Space-station mesh unavailable; grating regression skipped."));
				return;
			}
			const FVector Location = Origin + FVector(15000, 0, 0);
			AActor* Platform = Spawn<AActor>(Location);
			UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Platform);
			Platform->SetRootComponent(Mesh);
			Mesh->SetStaticMesh(Asset);
			Mesh->SetWorldLocation(Location);
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Mesh->SetCollisionResponseToAllChannels(ECR_Block);
			Mesh->RegisterComponent();
			Grating = Mesh;
			GratingPoint = Location + FVector(934.82f, 886.16f, 0.48f);
		}
		void CheckGratingQueries() {
			const UBodySetup* Body = Grating->GetStaticMesh()->GetBodySetup();
			Test->AddInfo(FString::Printf(TEXT("Grating: mesh at %s, bounds %s, collision %d, double %d, never cooked %d, triangles %d, trace mode %d"),
				*Grating->GetComponentLocation().ToCompactString(), *Grating->Bounds.BoxExtent.ToCompactString(), int32(Grating->GetCollisionEnabled()), Body->bDoubleSidedGeometry, Body->bNeverNeedsCookedCollisionData, Body->TriMeshGeometries.Num(), int32(Body->CollisionTraceFlag)));
			// Centroid of the downward-wound Dam.stwalk1 grating triangle reported by the player.
			FHitResult Hit;
			Test->TestTrue(TEXT("Grating blocks pawn capsule from above"), World->SweepSingleByChannel(Hit, GratingPoint + FVector(0, 0, 180), GratingPoint - FVector(0, 0, 100), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(38.f, 88.f)));
			Test->TestTrue(TEXT("Grating blocks traces from below"), World->LineTraceSingleByChannel(Hit, GratingPoint - FVector(0, 0, 100), GratingPoint + FVector(0, 0, 100), ECC_Visibility));
		}
		FAutomationTestBase* Test;
		UWorld* World;
		TArray<AActor*> Actors;
		UStaticMeshComponent* Grating = nullptr;
		FVector GratingPoint;
		AHumvee* Car = nullptr;
		ATruck* Trucks[2] = {};
		FVector Origin = FVector(-30000, -30000, 15000);
		FQuat Previous;
		float Next = 0.f, MaxStep = 0.f;
		int32 Stage = 0;
	};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFContentTest, "DesktopForces.Gameplay.ContentFollowup", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FDFContentTest::RunTest(const FString& Parameters) {
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (UWorld* World = Context.World(); World && World->IsGameWorld() && World->GetAuthGameMode()) {
			ADD_LATENT_AUTOMATION_COMMAND(FDFContentCheck(this, World));
			return true;
		}
	AddError(TEXT("Run in a standalone/listen game."));
	return false;
}

#endif
