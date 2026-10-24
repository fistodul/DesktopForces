#include "Game/Zombie/ZombieWeapons.h"
#include "Game/Zombie/ZombieGame.h"
#include "Player/DFCharacter.h"
#include "Engine/World.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "DFZombie"

AZombieKnife::AZombieKnife() {
	HitDamage = 67.f; // ZombieKnife.SlashDamage
	MeleeRange = 213.75f; // Range 95
	KnifeReturnTime = 4.5f; // ZombieKnife_Thrown.TimeBeforeReturn
}

void AZombieKnife::FireShot(bool bAlt) {
	const AZombieGame* GM = GetWorld()->GetAuthGameMode<AZombieGame>();
	HitDamage = GM && GM->IsNemesis(GetOwnerCharacter()) ? 134.f : 67.f; // Slash: SlashDamage * 2
	Super::FireShot(bAlt);
}

void AZombieKnife::GetThrowDamage(float& Body, float& Head) const {
	const AZombieGame* GM = GetWorld()->GetAuthGameMode<AZombieGame>();
	Body = GM && GM->IsNemesis(GetOwnerCharacter()) ? 316.f : 158.f;
	Head = Body * 3.f;
}

AZombieShotgun::AZombieShotgun() {
	DisplayName = LOCTEXT("Boomstick", "Zombie Boomstick");
	Pellets = 12; // NumShellFragments
	HitDamage = 15.f; // ProcessTraceHit: 15 vs 12
	MaxClips = 22;
	RecoilPower = 0.15f;
}

void AZombieShotgun::StartFire(bool bAlt) {
	ADFWeapon::StartFire(false); // slam fire: no barrel choice, no twin window
}

bool AZombieShotgun::TryFire(bool bAlt) {
	if (!HasAmmoInClip())
		return ADFWeapon::TryFire(bAlt);
	bTwin = false;
	SetState(EDFWeaponState::Firing, FireTime);
	FireShot(false);
	return true;
}

AZombieShot::AZombieShot() {
	AIRating = -1.f;
}

bool AZombieShot::TryFire(bool bAlt) {
	ADFCharacter* Me = GetOwnerCharacter();
	AZombieGame* GM = GetWorld()->GetAuthGameMode<AZombieGame>();
	if (bAlt && Me && GM && HasAmmoInClip()) {
		// ZombieShot.InjectDrug: an enemy with 100 health or less changes sides (+2)
		FHitResult Hit;
		const FVector Start = GetFireStart();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DFZombieInject), false, Me);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + GetAimRotation().Vector() * InjectRange, ECC_GameTraceChannel2, Params)) {
			ADFCharacter* Target = Cast<ADFCharacter>(Hit.GetActor());
			if (Target && Target->IsAlive() && Target->GetTeam() != Me->GetTeam()) {
				if (GM->Inject(Me, Target)) {
					SetState(EDFWeaponState::Firing, FireTime);
					UseAmmo();
				}
				return true;
			}
		}
	}
	return Super::TryFire(bAlt);
}

AZombieArmour::AZombieArmour() {
	DisplayName = LOCTEXT("ZombieArmour", "Zombie Armour");
	MaxCharge = 120;
	ArmorAbsorption = 80;
}

#undef LOCTEXT_NAMESPACE
