#include "Vehicles/ArmedAPC/ArmedAPC.h"
#include "Vehicles/DFVehicleTurret.h"
#include "Weapons/DFProjectile.h"

#define LOCTEXT_NAMESPACE "DFArmedAPC"

AArmedAPC::AArmedAPC() {
	DisplayName = LOCTEXT("ArmedAPC", "Armed APC");
	SeatPositions = { FVector(112.5f, 0.f, 58.5f), FVector(0.f, 0.f, 216.f) }; // aSeatsPos(1) Z 96: the hatch
	// ArmedAPC defaultproperties (uu -> cm, UE1 rotation units -> degrees)
	Turret = CreateDefaultSubobject<UDFVehicleTurret>(TEXT("Turret"));
	Turret->GunnerSeat = 1;
	Turret->TurretPivot = FVector(0.f, 0.f, 189.f); // Z 84
	Turret->GunOffset = FVector(67.5f, 0.f, 0.f); // X 30
	Turret->MuzzleOffset = FVector(90.f, 0.f, 0.f); // X 40
	Turret->PitchMin = -11.f; // -2000
	Turret->PitchMax = 49.4f; // 9000
	Turret->TurnRate = 164.8f; // 30000 / s
	Turret->TurretMeshName = TEXT("RageWeapons/HeavyMachineGunPickupCarryMesh");
	Turret->TurretDrawScale = 1.25f;
	Turret->FireInterval = 0.1f;
	Turret->MaxClipAmmo = 100;
	Turret->ReloadTime = 4.f;
	Turret->Spread = 0.03f;
	Turret->Damage = 25.f;
	Turret->MaxRange = 13500.f; // 6000 uu
}

AArmedAPCRocket::AArmedAPCRocket() {
	DisplayName = LOCTEXT("ArmedAPCRocket", "Armed APC (rockets)");
	Turret->TurnRate = 109.9f; // 20000 / s
	Turret->TurretMeshName = TEXT("RageWeapons/RocketLauncherPickupCarryMesh");
	Turret->FireInterval = 0.8f;
	Turret->MaxClipAmmo = 4;
	Turret->Spread = 0.01f;
	Turret->ProjectileClass = ARocket::StaticClass();
	Turret->ClassicFireSound = TEXT("WeaponSFX_RocketLauncher.Fire");
	Turret->ClassicReloadSound = TEXT("WeaponSFX_RocketLauncher.Reload");
	Turret->HUDIconRect = FIntRect(0, 128, 160, 192); // the rocket launcher's icon
}

#undef LOCTEXT_NAMESPACE
