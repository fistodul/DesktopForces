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

#undef LOCTEXT_NAMESPACE
