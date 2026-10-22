#include "Weapons/M18/M18.h"
#include "Core/DFContent.h"
#include "GameFramework/ProjectileMovementComponent.h"

#define LOCTEXT_NAMESPACE "ServerWeapons"

namespace
{
	constexpr float SmokeDuration = 15.f;   // M18.smokeDuration
}

AM18::AM18()
{
	ViewMesh = TEXT("M18/M18PickupCarryMesh");
	RequiredContent = TEXT("Props/M18/M18PickupCarryMesh");
	bAddToLoadout = true;
	HUDIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(FString::Printf(TEXT("%s/Textures/M18/Skins/M18image.M18image"), DF::UnverifiedRoot)));
	HUDIconRect = FIntRect(0, 168, 126, 232);   // WeaponIcon X=0 Y=168 W=126 H=64
	DisplayName = LOCTEXT("M18", "M18 Smoke");
	ProjectileClass = ASmokeGrenade::StaticClass();
	AltProjectileClass = ASmokeGrenade::StaticClass();
	ClassicSelectSound = TEXT("WeaponSFX_HeavyMachineGun.Select");
}

ASmokeGrenade::ASmokeGrenade()
{
	Damage = 0.f;
	DamageRadius = 0.f;
	ExplosionEffectScale = 0.f;
	ClassicExplosionSound.Empty();
	PropMesh = TEXT("M18/M18ThrownCarryMesh");
}

void ASmokeGrenade::Explode(const FVector& Location, const FVector& Normal)
{
	if (bExploded)
	{
		return;
	}
	bExploded = true;
	MulticastSmoke();
	// lies there smoking; gone once the last puff has faded (8 s lifetime)
	SetLifeSpan(SmokeDuration + 9.f);
}

void ASmokeGrenade::MulticastSmoke_Implementation()
{
	DF::SpawnEffectAttached(TEXT("SmokeGrenade"), GetRootComponent(), 1.f);
}

#undef LOCTEXT_NAMESPACE
