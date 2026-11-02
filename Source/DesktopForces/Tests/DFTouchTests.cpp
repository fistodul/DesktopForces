#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Weapons/Pistol/Pistol.h"
#include "Core/DFContent.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureLODSettings.h"
#include "Engine/World.h"
#include "Player/DFCharacter.h"
#include "Player/DFPlayerController.h"
#include "UI/DFHUD.h"
#include "Weapons/DFWeapon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFCoreArtTest, "DesktopForces.UI.CoreArt",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FDFCoreArtTest::RunTest(const FString& Parameters) {
	// with or without a Mobile Forces install: the core game has a HUD and weapons of its own
	for (const TCHAR* Name : { TEXT("Boxes"), TEXT("Backs"), TEXT("Icons"), TEXT("ScoreBoxes"), TEXT("MinimapNewIcons"), TEXT("CrossHairX") })
		TestNotNull(Name, DF::LoadClassic<UTexture2D>(FString(TEXT("Textures/Rage/")) + Name));
	TestNotNull(TEXT("Weapon icons"), DF::LoadClassic<UTexture2D>(TEXT("Textures/RageWeapons/WeaponIcons")));
	TestNotNull(TEXT("Weapon model"), DF::LoadClassic<UStaticMesh>(TEXT("Props/RageWeapons/PistolPickupCarryMesh")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFTextureGroupsTest, "DesktopForces.UI.TextureGroups",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FDFTextureGroupsTest::RunTest(const FString& Parameters) {
	// what a phone gets: Config/DefaultDeviceProfiles.ini, Tools/mfport/detail.toml
	UDeviceProfile* Android = UDeviceProfileManager::Get().FindProfile(TEXT("Android"), false);
	if (!TestNotNull(TEXT("Android device profile"), Android))
		return false;
	TestEqual(TEXT("Phones: world"), Android->GetTextureLODGroup(TEXTUREGROUP_World).MaxLODSize, 256);
	TestEqual(TEXT("Phones: seen up close"), Android->GetTextureLODGroup(TEXTUREGROUP_Project01).MaxLODSize, 512);
	TestEqual(TEXT("Phones: strips"), Android->GetTextureLODGroup(TEXTUREGROUP_Project02).MaxLODSize, 1024);
	UTexture2D* Wall = LoadObject<UTexture2D>(nullptr, TEXT("/Game/MF/Replacements/Textures/rage_marina/concrete/marina_concrete_15.marina_concrete_15"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (TestNotNull(TEXT("A listed wall"), Wall)) {
		TestEqual(TEXT("A listed wall is in the detail group"), int32(Wall->LODGroup), int32(TEXTUREGROUP_Project01));
#if WITH_EDITORONLY_DATA
		TestEqual(TEXT("A listed wall has 1024 px"), int32(Wall->Source.GetSizeX()), 1024); // GetSizeX: a placeholder while it compiles
#endif
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFTouchStripTest, "DesktopForces.UI.TouchStrip",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FDFTouchStripTest::RunTest(const FString& Parameters) {
	// the startup loadout closes by itself or on the key, whichever: wait, then a frame to draw
	const double Start = FPlatformTime::Seconds();
	TSharedRef<double> ReadySince = MakeShared<double>(0.0);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Start, ReadySince]() {
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (Context.World() && Context.World()->IsGameWorld())
				World = Context.World();
		ADFPlayerController* PC = World ? World->GetFirstPlayerController<ADFPlayerController>() : nullptr;
		ADFHUD* HUD = PC ? PC->GetHUD<ADFHUD>() : nullptr;
		ADFCharacter* C = PC ? PC->GetMFCharacter() : nullptr;
		const double Now = FPlatformTime::Seconds();
		if (!C || !HUD || C->IsInLoadout() || !C->GetCurrentWeapon()) {
			*ReadySince = 0.0;
			if (Now - Start > 20.0) {
				AddError(TEXT("Run this test in a game, alive, out of the loadout."));
				return true;
			}
			if (C && C->IsInLoadout() && FMath::Fmod(Now - Start, 3.0) < 0.05)
				PC->DFLoadout();
			return false;
		}
		if (C->GetInventory().Num() < 2)
			C->GiveWeapon(APistol::StaticClass(), false, true); // something to tap on the strip
		if (*ReadySince == 0.0)
			*ReadySince = Now;
		if (Now - *ReadySince < 0.5)
			return false;
		FVector2D Size;
		GEngine->GameViewport->GetViewportSize(Size);
		// the strip as last drawn: tap each item of the bottom row until one that isn't in hand
		ADFWeapon* Tapped = nullptr;
		FVector2D At;
		for (float X = 0.f; X < Size.X && !Tapped; X += 8.f) {
			At = FVector2D(X, Size.Y - 8.f);
			if (ADFWeapon* W = HUD->InventoryItemAt(At); W && W != C->GetCurrentWeapon())
				Tapped = W;
		}
		if (!TestNotNull(TEXT("A second carried item is on the strip"), Tapped))
			return true;
		TestNull(TEXT("Nothing to tap mid-screen"), HUD->InventoryItemAt(Size * 0.5f));
		TestTrue(TEXT("The tap is taken"), PC->InputTouch(FTouchId(), ETouchType::Began, At, 1.f, 0));
		TestTrue(TEXT("The tapped item comes up"), C->GetPendingWeapon() == Tapped || C->GetCurrentWeapon() == Tapped);
		return true;
	}));
	return true;
}

#endif
