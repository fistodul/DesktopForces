#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "AI/DFBotController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "Game/DFGameMode.h"
#include "Game/DFPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "Player/DFCharacter.h"
#include "Vehicles/Buggy/Buggy.h"
#include "Weapons/Pistol/Pistol.h"
#include "Weapons/PredatorMissile/PredatorMissile.h"
#include "TimerManager.h"

class FDFGameplayLifecycleCheck : public IAutomationLatentCommand {
public:
	FDFGameplayLifecycleCheck(FAutomationTestBase* InTest, UWorld* InWorld)
		: Test(InTest),
		  World(InWorld) {}

	virtual bool Update() override {
		const float Now = World->GetTimeSeconds();
		if (Stage == 1 && Now >= NextFight) {
			Bot->Fight(BotPawn, Target);
			NextFight = Now + 0.2f;
		}
		if (Now < Next)
			return false;
		switch (Stage++) {
		case 0: {
			Floor = Spawn<AActor>(Origin - FVector(0, 0, 50));
			UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
			Floor->SetRootComponent(Box);
			Box->InitBoxExtent(FVector(4000, 2000, 50));
			Box->SetWorldLocation(Origin - FVector(0, 0, 50));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->SetMobility(EComponentMobility::Static);
			Box->RegisterComponent();
			Car = Spawn<ABuggy>(Origin + FVector(0, 0, 160));
			Driver = Spawn<ADFCharacter>(Origin + FVector(0, 220, 90));
			Controller = Spawn<AAIController>(Origin);
			// Controllers without a PlayerState intentionally destroy themselves on pawn death.
			Controller->PlayerState = Spawn<ADFPlayerState>(Origin);
			Controller->PlayerState->SetOwner(Controller);
			Controller->Possess(Driver);
			Driver->SetInLoadout(false);
			BotPawn = Spawn<ADFCharacter>(Origin + FVector(1800, 600, 90));
			Target = Spawn<ADFCharacter>(Origin + FVector(2500, 600, 90));
			Target->SetCanBeDamaged(false);
			Bot = Spawn<ADFBotController>(Origin);
			Bot->bStationary = true;
			Bot->bPlaced = true;
			Bot->Possess(BotPawn);
			World->GetTimerManager().ClearTimer(Bot->ThinkTimer);
			BotPawn->SetInLoadout(false);
			Gun = BotPawn->GiveWeapon(APistol::StaticClass(), false);
			Gun->MaxClipAmmo = 2;
			Gun->MaxClips = 6;
			Gun->FireTime = 0.1f;
			Gun->ReloadTime = 0.6f;
			Gun->GiveFullAmmo();
			BotPawn->EquipWeapon(Gun);
			InitialAmmo = Gun->GetAmmoCount();
			Next = Now + 4.f;
			break;
		}
		case 1: {
			Test->TestTrue(TEXT("Bot fires beyond its first magazine"), Gun->GetAmmoCount() < InitialAmmo - 2);
			BotPawn->StopFire(false);
			Bot->bHoldingFire = false;
			BotPawn->ServerSetFeignDeath(true);
			Test->TestTrue(TEXT("Grounded soldier can feign death"), BotPawn->IsFeigningDeath());
			Test->TestTrue(TEXT("Feign death preserves life"), BotPawn->IsAlive());
			Test->TestEqual(TEXT("Feign death prevents walking"), BotPawn->GetCharacterMovement()->GetMaxSpeed(), 0.f);
			const int32 Before = BotPawn->GetHealth();
			BotPawn->TakeDamage(10.f, FDamageEvent(UDFDamage_Bullet::StaticClass()), nullptr, nullptr);
			Test->TestTrue(TEXT("Feigning soldier can still be damaged"), BotPawn->GetHealth() < Before);
			Test->TestTrue(TEXT("Enter driver seat"), Car->EnterSeat(Driver, 0));
			FHitResult Hit;
			const FVector Head = Driver->GetPawnViewLocation();
			World->LineTraceSingleByChannel(Hit, Head + FVector(0, 0, 400), Head, ECC_GameTraceChannel2);
			Test->TestEqual(TEXT("Open buggy exposes driver to weapon traces"), Hit.GetActor(), static_cast<AActor*>(Driver));
			Car->GetBody()->SetPhysicsLinearVelocity(FVector(1200, 0, 0));
			Car->Exit();
			Test->TestTrue(TEXT("Moving exit does not kill the driver"), Driver->IsAlive());
			Test->TestTrue(TEXT("Exit restores the controller to the soldier"), Controller->GetPawn() == Driver);
			Next = Now + 2.f;
			break;
		}
		case 2: {
			Test->TestTrue(TEXT("Exited driver remains alive above the floor"), Driver->IsAlive() && Driver->GetActorLocation().Z > Origin.Z);
			Test->TestTrue(TEXT("Exited driver retains collision"), Driver->GetActorEnableCollision() && Driver->GetCapsuleComponent()->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
			const int32 AmmoBefore = Gun->GetAmmoCount();
			BotPawn->StartFire(false);
			Test->TestFalse(TEXT("Fire stands up from feign death"), BotPawn->IsFeigningDeath());
			Test->TestEqual(TEXT("Wake-up press does not also shoot"), Gun->GetAmmoCount(), AmmoBefore);
			Driver->SetActorLocation(Car->GetActorLocation() + FVector(0, 220, 0), false, nullptr, ETeleportType::TeleportPhysics);
			Test->TestTrue(TEXT("Reenter before testing death"), Car->EnterSeat(Driver, 0));
			Driver->Die(nullptr, UDFDamage_Bullet::StaticClass(), nullptr);
			Test->TestFalse(TEXT("Death unpossesses the controller instead of retaining a corpse"), Controller->GetPawn() == Driver);
			Test->TestEqual(TEXT("Death does not resume falling movement"), Driver->GetCharacterMovement()->MovementMode, MOVE_None);
			Generator = Spawn<AVehicleGenerator>(Origin + FVector(-2000, 0, 0));
			Generator->VehicleClass = ABuggy::StaticClass();
			Car->Generator = Generator;
			Car->AbandonedLifetime = 0.1f;
			FActorSpawnParameters Params;
			Params.Instigator = BotPawn;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APredatorRocket* Missile = World->SpawnActor<APredatorRocket>(Origin + FVector(0, 0, 7000), FRotator::ZeroRotator, Params);
			Missile->SetActorTickEnabled(false);
			UParticleSystemComponent* Trail = FindTrail(Missile);
			Test->TestNotNull(TEXT("Predator flight has a trail"), Trail);
			Missile->Explode(Missile->GetActorLocation());
			Test->TestTrue(TEXT("Explosion stops trail emission immediately"), Trail && Trail->bSuppressSpawning);
			APredatorRocket* Removed = World->SpawnActor<APredatorRocket>(Origin + FVector(0, 0, 9000), FRotator::ZeroRotator, Params);
			UParticleSystemComponent* RemovedTrail = FindTrail(Removed);
			Removed->Destroy();
			Test->TestTrue(TEXT("Direct destruction also stops the pooled trail"), RemovedTrail && RemovedTrail->bSuppressSpawning);
			Next = Now + FMath::Max(9.f, float(World->GetAuthGameMode<ADFGameMode>()->RespawnWait + 1));
			break;
		}
		default: {
			ADFCharacter* Respawned = Cast<ADFCharacter>(Controller->GetPawn());
			Test->TestTrue(TEXT("Dead driver respawns as a living soldier"), Respawned && Respawned != Driver && Respawned->IsAlive());
			Test->TestTrue(TEXT("Abandoned generated vehicle expires"), !IsValid(Car) || Car->IsActorBeingDestroyed());
			bool bReplacement = false;
			for (TActorIterator<ADFVehicle> It(World); It; ++It)
				if (It->Generator == Generator && *It != Car) {
					bReplacement = true;
					It->Destroy();
				}
			Test->TestTrue(TEXT("Generator supplies a replacement"), bReplacement);
			if (Respawned)
				Respawned->Destroy();
			for (AActor* Actor : TArray<AActor*>{ Controller, Bot, Driver, BotPawn, Target, Generator, Floor })
				if (IsValid(Actor))
					Actor->Destroy();
			return true;
		}
		}
		return false;
	}

private:
	template <typename T>
	T* Spawn(const FVector& Location) {
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<T>(Location, FRotator::ZeroRotator, Params);
	}
	UParticleSystemComponent* FindTrail(AActor* Actor) {
		for (USceneComponent* Child : Actor->GetRootComponent()->GetAttachChildren())
			if (UParticleSystemComponent* Effect = Cast<UParticleSystemComponent>(Child))
				return Effect;
		return nullptr;
	}
	FAutomationTestBase* Test;
	UWorld* World;
	AActor* Floor = nullptr;
	ABuggy* Car = nullptr;
	AVehicleGenerator* Generator = nullptr;
	ADFCharacter *Driver = nullptr, *BotPawn = nullptr, *Target = nullptr;
	AAIController* Controller = nullptr;
	ADFBotController* Bot = nullptr;
	ADFWeapon* Gun = nullptr;
	const FVector Origin = FVector(50000, 50000, 5000);
	int32 Stage = 0, InitialAmmo = 0;
	float Next = 0.f, NextFight = 0.f;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFLifecycleTest, "DesktopForces.Gameplay.Lifecycle", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FDFLifecycleTest::RunTest(const FString& Parameters) {
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (UWorld* World = Context.World(); World && World->IsGameWorld() && World->GetAuthGameMode()) {
			ADD_LATENT_AUTOMATION_COMMAND(FDFGameplayLifecycleCheck(this, World));
			return true;
		}
	AddError(TEXT("Run this test in a standalone/listen game."));
	return false;
}

#endif
