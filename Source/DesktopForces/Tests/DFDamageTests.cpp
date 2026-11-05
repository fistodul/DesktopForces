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
	// a hull: measured to its surface (a Humvee, 4.8 m long, turned 90 degrees)
	const FTransform Humvee(FRotator(0.f, 90.f, 0.f), FVector(1000.f, 0.f, 0.f));
	const FVector Extent(240.f, 90.f, 60.f);
	Blast.Origin = FVector(1000.f, 240.f, 0.f); // against its nose
	TestEqual(TEXT("against the nose: all of it"), DF::RadialDamageScale(Blast, Humvee, Extent), 1.f);
	Blast.Origin = FVector(1000.f, 690.f, 0.f);
	TestEqual(TEXT("450 off the nose: half"), DF::RadialDamageScale(Blast, Humvee, Extent), 0.5f);
	return true;
}

#endif
