#include "Core/DFProps.h"
#include "Engine/DamageEvents.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFBlastFalloffTest, "DesktopForces.Gameplay.BlastFalloff", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDFBlastFalloffTest::RunTest(const FString& Parameters) {
	// Actor.HurtRadius: 1 - max(0, (dist - victim's radius) / radius)
	FRadialDamageEvent Blast;
	Blast.Origin = FVector::ZeroVector;
	Blast.Params.OuterRadius = 900.f;
	const float Soldier = 38.f;
	TestEqual(TEXT("touching the blast: all of it"), DF::RadialDamageScale(Blast, FVector(30.f, 0.f, 0.f), Soldier), 1.f);
	TestEqual(TEXT("half way out: half"), DF::RadialDamageScale(Blast, FVector(488.f, 0.f, 0.f), Soldier), 0.5f);
	TestEqual(TEXT("outside: none"), DF::RadialDamageScale(Blast, FVector(1000.f, 0.f, 0.f), Soldier), 0.f);
	TestEqual(TEXT("a shot isn't scaled"), DF::RadialDamageScale(FPointDamageEvent(), FVector(5000.f, 0.f, 0.f), Soldier), 1.f);
	return true;
}

#endif
