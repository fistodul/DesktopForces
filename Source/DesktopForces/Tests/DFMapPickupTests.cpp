#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/DFGameMode.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/DFCharacter.h"
#include "Weapons/AdrenalineShot/AdrenalineShot.h"
#include "Weapons/Pistol/Pistol.h"
#include "Weapons/Shotgun/Shotgun.h"
#include "World/DFPickupSpot.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFMapPickupTest, "DesktopForces.Gameplay.MapPickup", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FDFMapPickupTest::RunTest(const FString& Parameters) {
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (Context.World() && Context.World()->IsGameWorld() && Context.World()->GetAuthGameMode())
			World = Context.World();
	if (!World) {
		AddError(TEXT("Run pickup checks in a standalone/listen game."));
		return false;
	}
	ADFGameMode* Rules = World->GetAuthGameMode<ADFGameMode>();
	if (!Rules)
		return false;
	const int32 OldPlayerCount = Rules->NumPlayers;
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Position(-40000, -40000, 20000);
	ADFCharacter* Soldier = World->SpawnActor<ADFCharacter>(Position, FRotator::ZeroRotator, Spawn);
	Controller->Possess(Soldier);
	Soldier->SetInLoadout(false);
	Soldier->GetCharacterMovement()->DisableMovement();
	TArray<ADFPickupSpot*> Spots;
	TArray<ADFWeapon*> Items;
	auto Make = [&](TSubclassOf<ADFWeapon> Type, bool Auto) {
		ADFPickupSpot* Spot = World->SpawnActorDeferred<ADFPickupSpot>(ADFPickupSpot::StaticClass(), FTransform(Position + FVector(500, 0, 0)), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Spot->ItemClass = Type;
		Spot->bAutoPickup = Auto;
		Spot->RespawnTime = 0.f;
		Spot->FinishSpawning(Spot->GetActorTransform());
		Spots.Add(Spot);
		if (ADFWeapon* Item = Spot->Item.Get())
			Items.Add(Item);
		else
			AddError(TEXT("Pickup fixture did not spawn its item."));
		return Spot;
	};
	const int32 Capacity = Soldier->MaxCarry;
	Soldier->MaxCarry = 0;
	ADFPickupSpot* Auto = Make(APistol::StaticClass(), true);
	Auto->TryPickup(Soldier);
	TestTrue(TEXT("Full inventory leaves touch pickup available"), Auto->Item.IsValid() && Auto->Item->IsDropped());
	Soldier->MaxCarry = Capacity;
	Auto->TryPickup(Soldier);
	TestTrue(TEXT("Touch pickup transfers into inventory when space is available"), Auto->Item.IsValid() && Soldier->GetInventory().Contains(Auto->Item.Get()));
	TestTrue(TEXT("Taken item is no longer available on the ground"), Auto->Item.IsValid() && !Auto->Item->IsDropped());
	ADFPickupSpot* Duplicate = Make(APistol::StaticClass(), true);
	Duplicate->TryPickup(Soldier);
	TestTrue(TEXT("Per-class carry limit rejects a second pistol"), Duplicate->Item.IsValid() && Duplicate->Item->IsDropped());
	ADFPickupSpot* Manual = Make(AShotgun::StaticClass(), false);
	TestTrue(TEXT("Manual fixture could otherwise fit"), Soldier->CanCarry(AShotgun::StaticClass()));
	Manual->TryPickup(Soldier);
	TestTrue(TEXT("Ordinary pickup does not auto-collect"), Manual->Item.IsValid() && Manual->Item->IsDropped());
	ADFPickupSpot* Shot = Make(AAdrenalineShot::StaticClass(), true);
	if (Shot->Item.IsValid()) {
		UStaticMeshComponent* Shape = Shot->Item->FindComponentByClass<UStaticMeshComponent>();
		TestTrue(TEXT("No black fallback rectangle for meshless adrenaline"), Shape && !Shape->IsVisible());
	}
	ADFPickupSpot* Touch = Make(AShotgun::StaticClass(), true);
	// Let the new trigger enter the physics scene before testing a real pawn overlap.
	const double ReadyAt = World->GetTimeSeconds() + 0.25;
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>([this, World, ReadyAt, Soldier, Controller, Rules, OldPlayerCount, Touch, Items, Spots]() {
		if (World->GetTimeSeconds() < ReadyAt)
			return false;
		Soldier->SetActorLocation(Touch->GetActorLocation());
		Touch->PickupTouch->UpdateOverlaps();
		TestTrue(TEXT("Registered touch trigger transfers a weapon without direct pickup calls"), Touch->Item.IsValid() && Soldier->GetInventory().Contains(Touch->Item.Get()));
		for (ADFWeapon* Item : Items)
			Item->Destroy();
		for (ADFPickupSpot* Spot : Spots)
			Spot->Destroy();
		Soldier->Destroy();
		Controller->Destroy();
		Rules->NumPlayers = OldPlayerCount;
		return true;
	}));
	return true;
}
#endif
