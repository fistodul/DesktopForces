#include "Weapons/Totem/Totem.h"
#include "Player/DFCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "Totem"

namespace TotemLocal {
	void PlayAt(const AActor* Where, const TCHAR* Path) {
		if (Where)
			UGameplayStatics::PlaySoundAtLocation(Where, LoadObject<USoundBase>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet), Where->GetActorLocation());
	}
	const TCHAR* TotemSound = TEXT("/MobileForcesClassic/Sounds/MiscSFX/ArmourWearOut.ArmourWearOut");
} // namespace TotemLocal

// --- heal over time ---------------------------------------------------------------------

UTotemHealComponent::UTotemHealComponent() {
	PrimaryComponentTick.bCanEverTick = false;
}

UTotemHealComponent* UTotemHealComponent::Apply(ADFCharacter* Target, float InHealPerTick, int32 InTicks, float InInterval, float InGrace) {
	if (!Target || !Target->HasAuthority())
		return nullptr;
	UTotemHealComponent* C = NewObject<UTotemHealComponent>(Target);
	C->HealPerTick = InHealPerTick;
	C->TicksLeft = InTicks;
	C->Interval = FMath::Max(0.05f, InInterval);
	C->GraceLeft = InGrace;
	C->RegisterComponent();
	Target->GetWorldTimerManager().SetTimer(C->Timer, C, &UTotemHealComponent::HealTick, C->Interval, true);
	return C;
}

int32 UTotemHealComponent::FilterDamage(int32 Damage, const UDFDamageType* Type) {
	// TotemHealEffect.ArmorAbsorbDamage: nothing gets through the grace period (crushing does,
	// MF 'Crushed'; there are no movers yet)
	return GraceLeft > 0.f ? 0 : Damage;
}

void UTotemHealComponent::HealTick() {
	ADFCharacter* C = Cast<ADFCharacter>(GetOwner());
	if (!C || !C->IsAlive()) {
		DestroyComponent();
		return;
	}
	GraceLeft = FMath::Max(0.f, GraceLeft - Interval);
	// whole points only, the rest carries over (HealAccumulator)
	Accumulator += HealPerTick;
	if (Accumulator >= 1.f) {
		const int32 Points = FMath::FloorToInt(Accumulator);
		Accumulator -= Points;
		if (C->GetHealth() < C->MaxHealth)
			C->Heal(Points);
		else
			Accumulator = 0.f;
	}
	if (--TicksLeft <= 0 && GraceLeft <= 0.f) {
		GetWorld()->GetTimerManager().ClearTimer(Timer);
		DestroyComponent();
	}
}

// --- the item ---------------------------------------------------------------------------

ATotem::ATotem() {
	DisplayName = LOCTEXT("Totem", "Totem of Undying");
	InventoryGroup = 10;
	CarrySize = 1;
	MaxClipAmmo = 1;
	MaxClips = 1;
	bDestroyWhenEmpty = true;
	AutoSwitchPriority = 0.f;
	AIRating = -1.f;
	bAddToLoadout = true; // in every loadout crate while the plugin is enabled
	// instant like the adrenaline shot (no first-person animations)
	FireTime = 0.2f;
	EquipTime = 0.f;
	PutDownTime = 0.f;
	RecoilPower = 0.f;
	// Icon converted from the mod's own art (SourceArt/Mods/Icons/TotemIcon.bmp)
	HUDIcon = TEXT("/Game/MF/Mods/Icons/TotemIcon");
	HUDIconRect = FIntRect(0, 0, 64, 64);
}

bool ATotem::IsFriendly(const ADFCharacter* Other) const {
	const ADFCharacter* Me = GetOwnerCharacter();
	if (!Other || !Me)
		return false;
	return Other == Me || (Me->GetTeam() != DF::NoTeam && Other->GetTeam() == Me->GetTeam());
}

void ATotem::Message(const FString& Text) const {
	const ADFCharacter* Me = GetOwnerCharacter();
	if (APlayerController* PC = Me ? Cast<APlayerController>(Me->GetController()) : nullptr)
		PC->ClientMessage(Text);
}

void ATotem::UseCharge() {
	UseAmmo();
	// Finish() hands the empty item back (bDestroyWhenEmpty -> OnDepleted)
}

int32 ATotem::AbsorbDamage(int32 Damage, const UDFDamageType* Type) {
	// Totem.ArmorAbsorbDamage: a blow that would kill leaves you on SurviveHealth instead
	ADFCharacter* Me = GetOwnerCharacter();
	if (!Me || !HasAuthority() || !HasAmmoInClip() || Damage < Me->GetHealth())
		return Damage;
	const int32 Allowed = FMath::Max(0, Me->GetHealth() - SurviveHealth);
	UTotemHealComponent::Apply(Me, ReviveHealPerTick, ReviveTicks, HealInterval, ReviveGrace);
	Message(TEXT("Totem of Undying triggered! Death prevented."));
	TotemLocal::PlayAt(Me, TotemLocal::TotemSound);
	ClipAmmo = 0;
	OnDepleted(); // gone at once, it may not be the held item
	return Allowed;
}

bool ATotem::TryFire(bool bAlt) {
	ADFCharacter* Me = GetOwnerCharacter();
	if (!Me || !Me->IsAlive() || Me->IsInLoadout() || !HasAmmoInClip())
		return false;
	if (!HasAuthority()) {
		SetState(EDFWeaponState::Firing, FireTime); // the server decides who gets healed
		return true;
	}
	if (!bAlt) {
		// PulseHeal: everyone friendly in view within the radius who is hurt
		int32 Healed = 0;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DFTotemPulse), false, Me);
		for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It) {
			ADFCharacter* C = *It;
			if (!IsFriendly(C) || !C->IsAlive() || C->GetHealth() >= C->MaxHealth || FVector::Dist(C->GetActorLocation(), Me->GetActorLocation()) > PulseRadius)
				continue;
			FHitResult Hit;
			if (C != Me && GetWorld()->LineTraceSingleByChannel(Hit, Me->GetActorLocation(), C->GetActorLocation(), ECC_Visibility, Params) && Hit.GetActor() != C)
				continue; // VisibleCollidingActors
			C->Heal(PulseHeal);
			++Healed;
		}
		if (Healed == 0) {
			Message(TEXT("No one needs healing."));
			return false;
		}
		Message(FString::Printf(TEXT("AoE Pulse Heal activated! (%d healed)"), Healed));
		TotemLocal::PlayAt(Me, TotemLocal::TotemSound);
	} else {
		// TryTargetedHoT: the team-mate in the crosshair, close by
		FHitResult Hit;
		const FVector Start = GetFireStart();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DFTotemTarget), false, Me);
		ADFCharacter* Target = nullptr;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + GetAimRotation().Vector() * TargetRange, ECC_GameTraceChannel2, Params))
			Target = Cast<ADFCharacter>(Hit.GetActor());
		if (!Target || Target == Me || !IsFriendly(Target) || !Target->IsAlive()) {
			Message(TEXT("Target not found."));
			return false;
		}
		if (Target->GetHealth() >= Target->MaxHealth) {
			Message(TEXT("Target is already at full health."));
			return false;
		}
		UTotemHealComponent::Apply(Target, AllyHealPerTick, HealTicks, HealInterval, 0.f);
		Message(TEXT("Injected ally with Sustained Regeneration!"));
	}
	SetState(EDFWeaponState::Firing, FireTime);
	UseCharge();
	return true;
}

void ATotem::StartReload() {
	// Reload: the sustained heal on yourself (no ammo to reload)
	if (GetLocalRole() < ROLE_Authority) {
		Super::StartReload(); // sends ServerReload
		return;
	}
	ADFCharacter* Me = GetOwnerCharacter();
	if (!Me || !Me->IsAlive() || Me->GetCurrentWeapon() != this || State != EDFWeaponState::Idle || !HasAmmoInClip())
		return;
	if (Me->GetHealth() >= Me->MaxHealth) {
		Message(TEXT("Already at full health."));
		return;
	}
	UTotemHealComponent::Apply(Me, SelfHealPerTick, HealTicks, HealInterval, 0.f);
	Message(TEXT("Sustained Regeneration activated."));
	SetState(EDFWeaponState::Firing, FireTime);
	UseCharge();
}

#undef LOCTEXT_NAMESPACE
