#include "Game/Zombie/MFZombieWeapons.h"
#include "Game/Zombie/MFGameMode_Zombie.h"
#include "Player/MFCharacter.h"
#include "Engine/World.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "MFZombie"

AMFWeapon_ZombieKnife::AMFWeapon_ZombieKnife()
{
	HitDamage = 67.f;          // ZombieKnife.SlashDamage
	MeleeRange = 213.75f;      // Range 95
	KnifeReturnTime = 4.5f;    // ZombieKnife_Thrown.TimeBeforeReturn
}

void AMFWeapon_ZombieKnife::FireShot(bool bAlt)
{
	const AMFGameMode_Zombie* GM = GetWorld()->GetAuthGameMode<AMFGameMode_Zombie>();
	HitDamage = GM && GM->IsNemesis(GetOwnerCharacter()) ? 134.f : 67.f;   // Slash: SlashDamage * 2
	Super::FireShot(bAlt);
}

void AMFWeapon_ZombieKnife::GetThrowDamage(float& Body, float& Head) const
{
	const AMFGameMode_Zombie* GM = GetWorld()->GetAuthGameMode<AMFGameMode_Zombie>();
	Body = GM && GM->IsNemesis(GetOwnerCharacter()) ? 316.f : 158.f;
	Head = Body * 3.f;
}

AMFWeapon_ZombieShotgun::AMFWeapon_ZombieShotgun()
{
	DisplayName = LOCTEXT("Boomstick", "Zombie Boomstick");
	Pellets = 12;              // NumShellFragments
	HitDamage = 15.f;          // ProcessTraceHit: 15 vs 12
	MaxClips = 22;
	RecoilPower = 0.15f;
}

void AMFWeapon_ZombieShotgun::StartFire(bool bAlt)
{
	AMFWeapon::StartFire(false);   // slam fire: no barrel choice, no twin window
}

bool AMFWeapon_ZombieShotgun::TryFire(bool bAlt)
{
	if (!HasAmmoInClip())
	{
		return AMFWeapon::TryFire(bAlt);
	}
	bTwin = false;
	SetState(EMFWeaponState::Firing, FireTime);
	FireShot(false);
	return true;
}

AMFWeapon_ZombieShot::AMFWeapon_ZombieShot()
{
	AIRating = -1.f;
}

bool AMFWeapon_ZombieShot::TryFire(bool bAlt)
{
	AMFCharacter* Me = GetOwnerCharacter();
	AMFGameMode_Zombie* GM = GetWorld()->GetAuthGameMode<AMFGameMode_Zombie>();
	if (bAlt && Me && GM && HasAmmoInClip())
	{
		// ZombieShot.InjectDrug: an enemy with 100 health or less changes sides (+2)
		FHitResult Hit;
		const FVector Start = GetFireStart();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MFZombieInject), false, Me);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + GetAimRotation().Vector() * InjectRange, ECC_GameTraceChannel2, Params))
		{
			AMFCharacter* Target = Cast<AMFCharacter>(Hit.GetActor());
			if (Target && Target->IsAlive() && Target->GetTeam() != Me->GetTeam())
			{
				if (GM->Inject(Me, Target))
				{
					SetState(EMFWeaponState::Firing, FireTime);
					ClipAmmo -= 1;
				}
				return true;
			}
		}
	}
	return Super::TryFire(bAlt);
}

AMFWeapon_ZombieArmour::AMFWeapon_ZombieArmour()
{
	DisplayName = LOCTEXT("ZombieArmour", "Zombie Armour");
	MaxCharge = 120;
	ArmorAbsorption = 80;
}

#undef LOCTEXT_NAMESPACE
