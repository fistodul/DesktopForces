#include "Weapons/GrenadeLauncher/GrenadeLauncher.h"
#include "Core/DFContent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"

#define LOCTEXT_NAMESPACE "DFGrenadeLauncher"

ALauncherGrenade::ALauncherGrenade() {
	Movement->InitialSpeed = 7875.f; // primaryFireProjectileSpeed 3500 uu/s
	Movement->MaxSpeed = 7875.f;
	Damage = 250.f; // primaryFireDamage
	DamageRadius = 450.f; // primaryFireDamageRadius 200 uu
	FuseTime = 8.f;
}

AGrenadeFragment::AGrenadeFragment() {
	Damage = 20.f; // altFireFragmentDamageSum 300 / 15
	DamageRadius = 450.f; // altFireSingleFragmentDamageRadius 200 uu
	Momentum = 6000.f; // MomentumTransfer / 15
	LaunchUpVelocity = 0.f;
	FuseTime = 4.f;
	Collision->InitSphereRadius(8.f);
}

AClusterGrenade::AClusterGrenade() {
	Movement->InitialSpeed = 7875.f; // altFireProjectileSpeed 3500 uu/s
	Movement->MaxSpeed = 7875.f;
	Damage = 0.f; // the fragments do the damage
	DamageRadius = 0.f;
	FuseTime = 8.f;
}

void AClusterGrenade::Explode(const FVector& Location, const FVector& Normal) {
	if (bExploded)
		return;
	if (HasAuthority()) {
		// launcherGrenade.Explosion: fragments in pairs flung to opposite sides, up and away from
		// the surface it hit
		FActorSpawnParameters SP;
		SP.Owner = GetOwner();
		SP.Instigator = GetInstigator();
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		FVector Force = FVector::ZeroVector;
		for (int32 i = 0; i < FragmentCount; ++i) {
			if (i % 2 == 0) {
				Force.X = FMath::RandRange(-64, 63) * FragmentSpread;
				Force.Y = FMath::RandRange(-64, 63) * FragmentSpread;
			} else {
				Force.X = -Force.X + FMath::RandRange(-1, 0);
				Force.Y = -Force.Y + FMath::RandRange(-1, 0);
			}
			Force.Z = FMath::RandRange(16, 79);
			FVector Vel = Force * 5.f * 2.25f; // uu/s -> cm/s
			if (Normal.Z < -0.1f)
				Vel = Vel.MirrorByVector(Normal);
			if (ADFProjectile* Frag = GetWorld()->SpawnActor<ADFProjectile>(AGrenadeFragment::StaticClass(), Location + Normal * 20.f, Vel.Rotation(), SP))
				Frag->Launch(Vel.GetSafeNormal(), Vel.Size(), FVector::ZeroVector);
		}
	}
	Super::Explode(Location, Normal);
}

AGrenadeLauncher::AGrenadeLauncher() {
	DisplayName = LOCTEXT("GrenadeLauncher", "Grenade Launcher");
	ViewMesh = TEXT("grenLaunch/grenLaunchPickupCarryMesh");
	ArmsMesh = TEXT("grenLaunchPlayerMesh"); // PlayerViewMesh, PlayerViewScale, PlayerViewOffset
	ArmsScale = 0.2f;
	ArmsOffset = FVector(0.f, 0.25f, -3.5f);
	HUDIcon = TEXT("Textures/grenLaunch/Skins/grenLaunchimage");
	HUDIconRect = FIntRect(0, 196, 128, 266); // WeaponIcon Y=196 W=128 H=70
	bAddToLoadout = true;
	InventoryGroup = 7;
	CarrySize = 4;
	AIRating = 0.55f;
	MaxClipAmmo = 1;
	MaxClips = 6; // Pistol's 6
	FireTime = 0.6f;
	ReloadTime = 1.5f;
	EquipTime = 0.667f;
	PutDownTime = 0.5f;
	bInstantHit = false;
	ProjectileClass = ALauncherGrenade::StaticClass();
	AltProjectileClass = AClusterGrenade::StaticClass();
	MaxZoomLevel = 0;
	RecoilPower = 3.8f;
	ClassicFireSound = TEXT("gren.grenfire");
	ClassicReloadSound = TEXT("gren.grenadereload");
	ClassicSelectSound = TEXT("WeaponSFX_RocketLauncher.Select");
}

#undef LOCTEXT_NAMESPACE
