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
	// the HMG (core content) draws MF's canvas flash; give it a muzzle bone for the world one
	AHeavyMachineGun* Gun = World->SpawnActor<AHeavyMachineGun>();
	if (Gun) {
		Gun->MuzzleBone = TEXT("GunBarrel");
		Gun->MuzzleOffset = FVector(-0.6f, 221.7f, 0.1f);
	}
	if (!TestTrue(TEXT("A gun with a muzzle bone flashes there"), Gun && Gun->UsesWorldMuzzleFlash())) {
		if (Gun)
			Gun->Destroy();
		return false;
	}
	TestNull(TEXT("No flash component before the first shot"), Gun->MuzzleSprite.Get());
	Gun->OnGiven(Soldier);
	Gun->OnEquip();
	Gun->RecoilPower = 0.f;
	Gun->FireSound = nullptr;
	Gun->PlayFireEffects(false);
	if (!TestNotNull(TEXT("The first shot makes the flash component"), Gun->MuzzleSprite.Get())) {
		Gun->Destroy();
		return false;
	}
	UMaterialInterface* FlashMaterial = Gun->MuzzleSprite->GetMaterial(0);
	TestTrue(TEXT("Flash material lets the weapon occlude it"), FlashMaterial && FlashMaterial->GetMaterial() && !FlashMaterial->GetMaterial()->bDisableDepthTest);
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
