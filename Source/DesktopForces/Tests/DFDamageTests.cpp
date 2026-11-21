#include "Core/DFProps.h"
#include "Core/DFTypes.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Player/DFCharacter.h"
#include "Vehicles/Armadillo/Armadillo.h"
#include "Weapons/SentryGun/SentryGun.h"
#include "World/DFZoneVolume.h"

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

namespace {
	UWorld* FindGameWorld() {
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (UWorld* World = Context.World(); World && World->IsGameWorld())
				return World;
		return nullptr;
	}
} // namespace

// Map hazards were made for 100-health soldiers: one that hits for 100 or more kills anyone
// (a 333-health zombie at the bottom of a death pit), a lesser one stays as it is.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFHazardTest, "DesktopForces.Gameplay.LethalHazards", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FDFHazardTest::RunTest(const FString& Parameters) {
	UWorld* World = FindGameWorld();
	if (!World) {
		AddError(TEXT("No game world."));
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADFCharacter* Zombie = World->SpawnActor<ADFCharacter>(FVector(-30000.f, -30000.f, 20000.f), FRotator::ZeroRotator, Spawn);
	if (!Zombie) {
		AddError(TEXT("Could not spawn a soldier."));
		return false;
	}
	Zombie->SetInLoadout(false);
	Zombie->SetHealthAndMax(333);
	Zombie->TakeDamage(40.f, FDamageEvent(UDFDamage_Zone::StaticClass()), nullptr, nullptr);
	TestEqual(TEXT("A 40 pain zone tick takes 40"), Zombie->GetHealth(), 293);
	Zombie->TakeDamage(99.f, FDamageEvent(UDFDamage_Fell::StaticClass()), nullptr, nullptr);
	TestEqual(TEXT("A fall short of FallDeathThreshold takes what it did"), Zombie->GetHealth(), 194);
	Zombie->TakeDamage(100.f, FDamageEvent(UDFDamage_Fell::StaticClass()), nullptr, nullptr);
	TestFalse(TEXT("A fall past FallDeathThreshold kills a 333-health zombie"), Zombie->IsAlive());
	Zombie->Destroy();
	return true;
}

// Sentry rounds pierce a bulletproof hull (1 a shot on an Armadillo); bullets don't.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFArmourPiercingTest, "DesktopForces.Gameplay.ArmourPiercing", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FDFArmourPiercingTest::RunTest(const FString& Parameters) {
	UWorld* World = FindGameWorld();
	if (!World) {
		AddError(TEXT("No game world."));
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AArmadillo* Apc = World->SpawnActor<AArmadillo>(FVector(-30000.f, -36000.f, 20000.f), FRotator::ZeroRotator, Spawn);
	if (!Apc) {
		AddError(TEXT("Could not spawn an Armadillo."));
		return false;
	}
	const int32 Before = Apc->Health;
	Apc->TakeDamage(20.f, FDamageEvent(UDFDamage_Bullet::StaticClass()), nullptr, nullptr);
	TestEqual(TEXT("A bullet doesn't get through the Armadillo"), Apc->Health, Before);
	Apc->TakeDamage(20.f, FDamageEvent(UDFDamage_Sentry::StaticClass()), nullptr, nullptr);
	TestEqual(TEXT("A sentry round does"), Apc->Health, Before - 1);
	Apc->Destroy();
	return true;
}

#endif
