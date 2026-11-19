#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/MaterialBillboardComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/Material.h"
#include "Player/DFCharacter.h"
#include "TimerManager.h"
#include "Weapons/HeavyMachineGun/HeavyMachineGun.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFMuzzleFlashTest, "DesktopForces.Gameplay.MuzzleFlash", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FDFMuzzleFlashTest::RunTest(const FString& Parameters) {
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (Context.World() && Context.World()->IsGameWorld() && Context.World()->GetFirstPlayerController())
			World = Context.World();
	ADFCharacter* Soldier = World ? Cast<ADFCharacter>(World->GetFirstPlayerController()->GetPawn()) : nullptr;
	if (!Soldier || !Soldier->IsLocallyControlled()) {
		AddError(TEXT("Muzzle check needs a locally controlled soldier."));
		return false;
	}
	AHeavyMachineGun* Gun = World->SpawnActor<AHeavyMachineGun>();
	if (!TestTrue(TEXT("HMG has a depth-tested flash component"), Gun && Gun->UsesWorldMuzzleFlash())) {
		if (Gun)
			Gun->Destroy();
		return false;
	}
	UMaterialInterface* FlashMaterial = Gun->MuzzleSprite->GetMaterial(0);
	TestTrue(TEXT("Flash material lets the weapon occlude it"), FlashMaterial && FlashMaterial->GetMaterial() && !FlashMaterial->GetMaterial()->bDisableDepthTest);
	Gun->OnGiven(Soldier);
	Gun->OnEquip();
	Gun->RecoilPower = 0.f;
	Gun->FireSound = nullptr;
	Gun->PlayFireEffects(false);
	TestTrue(TEXT("Shot enables the world flash"), Gun->MuzzleSprite->IsVisible());
	const double ReadyAt = World->GetTimeSeconds() + 0.1;
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>([this, World, ReadyAt, Gun]() {
		if (World->GetTimeSeconds() < ReadyAt)
			return false;
		TestFalse(TEXT("Flash expires without another shot"), Gun->MuzzleSprite->IsVisible());
		Gun->PlayFireEffects(false);
		TestTrue(TEXT("A later shot can enable the flash again"), Gun->MuzzleSprite->IsVisible());
		Gun->ForceInactive();
		TestFalse(TEXT("Holstering immediately hides the flash"), Gun->MuzzleSprite->IsVisible());
		TestFalse(TEXT("Holstering cancels the flash timer"), World->GetTimerManager().IsTimerActive(Gun->MuzzleFlashTimer));
		Gun->Destroy();
		return true;
	}));
	return true;
}
#endif
