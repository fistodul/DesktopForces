#include "Weapons/ImprovedRL/ImprovedRL.h"
#include "GameFramework/ProjectileMovementComponent.h"

#define LOCTEXT_NAMESPACE "DFWeapons"

AImprovedRL::AImprovedRL()
{
	DisplayName = LOCTEXT("ImprovedRL", "Improved RL");
	CarrySize = 3;
	ProjectileClass = AImprovedRocket::StaticClass();
	AltProjectileClass = AImprovedRocket::StaticClass();
	bAddToLoadout = true;
}

AImprovedRocket::AImprovedRocket()
{
	Movement->InitialSpeed = 4050.f;          // ImprovedRocket.speed 1800 uu/s
	Damage = 140.f;
}

#undef LOCTEXT_NAMESPACE
