#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/DFGameMode.h"
#include "Game/DFPlayerStart.h"
#include "Game/DFPlayerState.h"
#include "NavigationSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFTestRangeStartsTest, "DesktopForces.Maps.TestRange.TeamStarts",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FDFTestRangeStartsTest::RunTest(const FString& Parameters) {
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (UWorld* Candidate = Context.World(); Candidate && Candidate->IsGameWorld() && Candidate->GetAuthGameMode()) {
			World = Candidate;
			break;
		}
	if (!World || !World->GetMapName().EndsWith(TEXT("MF_TestRange"))) {
		AddError(TEXT("Run this test in MF_TestRange with a team game mode."));
		return false;
	}
	ADFGameMode* Mode = World->GetAuthGameMode<ADFGameMode>();
	if (!Mode || !Mode->IsTeamGame()) {
		AddError(TEXT("Run MF_TestRange in TDM to test both teams' start selection."));
		return false;
	}

	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	TestNotNull(TEXT("Saved map has a navigation system"), Navigation);
	int32 Neutral = 0;
	int32 TeamStarts[2] = {};
	for (TActorIterator<ADFPlayerStart> It(World); It; ++It) {
		TestTrue(TEXT("Test range start is enabled"), It->bEnabled);
		TestFalse(TEXT("Ordinary starts are not captain-only"), It->bCaptainStart);
		if (It->IsNeutral())
			++Neutral;
		else
			++TeamStarts[It->TeamNumber];
		if (Navigation) {
			FNavLocation Point;
			TestTrue(FString::Printf(TEXT("%s lies over the saved navmesh"), *It->GetName()),
				Navigation->ProjectPointToNavigation(It->GetActorLocation(), Point, FVector(100, 100, 250)));
		}
	}
	TestEqual(TEXT("Four neutral starts survive saving"), Neutral, 4);
	TestEqual(TEXT("Two blue starts survive saving"), TeamStarts[0], 2);
	TestEqual(TEXT("Two red starts survive saving"), TeamStarts[1], 2);

	for (uint8 Team = 0; Team < 2; ++Team) {
		AAIController* Controller = World->SpawnActor<AAIController>();
		ADFPlayerState* State = World->SpawnActor<ADFPlayerState>();
		State->SetTeam(Team);
		State->SetOwner(Controller);
		Controller->PlayerState = State;
		ADFPlayerStart* Start = Cast<ADFPlayerStart>(Mode->ChoosePlayerStart(Controller));
		if (TestNotNull(TEXT("Team selects a real DF player start"), Start))
			TestEqual(TEXT("Selected start belongs to the requested team"), Start->TeamNumber, Team);
		Controller->Destroy();
		State->Destroy();
	}
	return true;
}

#endif
