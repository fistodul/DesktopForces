#include "Mods/Totem/MFTotem.h"
#include "Player/MFCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "MFModTotem"

namespace
{
	void PlayAt(const AActor* Where, const TCHAR* Path)
	{
		if (Where)
		{
			UGameplayStatics::PlaySoundAtLocation(Where, LoadObject<USoundBase>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet), Where->GetActorLocation());
		}
	}
	const TCHAR* TotemSound = TEXT("/MobileForcesClassic/Sounds/MiscSFX/ArmourWearOut.ArmourWearOut");
}

// --- heal over time ---------------------------------------------------------------------

UMFTotemHealComponent::UMFTotemHealComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UMFTotemHealComponent* UMFTotemHealComponent::Apply(AMFCharacter* Target, float InHealPerTick, int32 InTicks, float InInterval, float InGrace)
{
	if (!Target || !Target->HasAuthority())
	{
		return nullptr;
	}
	UMFTotemHealComponent* C = NewObject<UMFTotemHealComponent>(Target);
	C->HealPerTick = InHealPerTick;
	C->TicksLeft = InTicks;
	C->Interval = FMath::Max(0.05f, InInterval);
	C->GraceLeft = InGrace;
	C->RegisterComponent();
	Target->GetWorldTimerManager().SetTimer(C->Timer, C, &UMFTotemHealComponent::HealTick, C->Interval, true);
	return C;
}

int32 UMFTotemHealComponent::FilterDamage(int32 Damage, const UMFDamageType* Type)
{
	// TotemHealEffect.ArmorAbsorbDamage: nothing gets through the grace period (crushing does,
	// MF 'Crushed'; there are no movers yet)
	return GraceLeft > 0.f ? 0 : Damage;
}

void UMFTotemHealComponent::HealTick()
{
	AMFCharacter* C = Cast<AMFCharacter>(GetOwner());
	if (!C || !C->IsAlive())
	{
		DestroyComponent();
		return;
	}
	GraceLeft = FMath::Max(0.f, GraceLeft - Interval);
	// whole points only, the rest carries over (HealAccumulator)
	Accumulator += HealPerTick;
	if (Accumulator >= 1.f)
	{
		const int32 Points = FMath::FloorToInt(Accumulator);
		Accumulator -= Points;
		if (C->GetHealth() < C->MaxHealth)
		{
			C->Heal(Points);
		}
		else
		{
			Accumulator = 0.f;
		}
	}
	if (--TicksLeft <= 0 && GraceLeft <= 0.f)
	{
		GetWorld()->GetTimerManager().ClearTimer(Timer);
		DestroyComponent();
	}
}

// --- the item ---------------------------------------------------------------------------

AMFWeapon_Totem::AMFWeapon_Totem()
{
	DisplayName = LOCTEXT("Totem", "Totem of Undying");
	InventoryGroup = 10;
	CarrySize = 1;
	MaxClipAmmo = 1;
	MaxClips = 1;
	bDestroyWhenEmpty = true;
	AutoSwitchPriority = 0.f;
	AIRating = -1.f;
	bAddToLoadout = true;   // in every loadout crate while the plugin is enabled
	// instant like the adrenaline shot (no first-person animations)
	FireTime = 0.2f;
	EquipTime = 0.f;
	PutDownTime = 0.f;
	RecoilPower = 0.f;
	// Icon converted from the mod's own art (SourceArt/Mods/Icons/TotemIcon.bmp)
	HUDIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/MF/Mods/Icons/TotemIcon.TotemIcon")));
	HUDIconRect = FIntRect(0, 0, 64, 64);
}

bool AMFWeapon_Totem::IsFriendly(const AMFCharacter* Other) const
{
	const AMFCharacter* Me = GetOwnerCharacter();
	if (!Other || !Me)
	{
		return false;
	}
	return Other == Me || (Me->GetTeam() != MF::NoTeam && Other->GetTeam() == Me->GetTeam());
}

void AMFWeapon_Totem::Message(const FString& Text) const
{
	const AMFCharacter* Me = GetOwnerCharacter();
	if (APlayerController* PC = Me ? Cast<APlayerController>(Me->GetController()) : nullptr)
	{
		PC->ClientMessage(Text);
	}
}

void AMFWeapon_Totem::UseCharge()
{
	ClipAmmo = FMath::Max(0, ClipAmmo - 1);
	// Finish() hands the empty item back (bDestroyWhenEmpty -> OnDepleted)
}

int32 AMFWeapon_Totem::AbsorbDamage(int32 Damage, const UMFDamageType* Type)
{
	// Totem.ArmorAbsorbDamage: a blow that would kill leaves you on SurviveHealth instead
	AMFCharacter* Me = GetOwnerCharacter();
	if (!Me || !HasAuthority() || !HasAmmoInClip() || Damage < Me->GetHealth())
	{
		return Damage;
	}
	const int32 Allowed = FMath::Max(0, Me->GetHealth() - SurviveHealth);
	UMFTotemHealComponent::Apply(Me, ReviveHealPerTick, ReviveTicks, HealInterval, ReviveGrace);
	Message(TEXT("Totem of Undying triggered! Death prevented."));
	PlayAt(Me, TotemSound);
	ClipAmmo = 0;
	OnDepleted();   // gone at once, it may not be the held item
	return Allowed;
}

bool AMFWeapon_Totem::TryFire(bool bAlt)
{
	AMFCharacter* Me = GetOwnerCharacter();
	if (!Me || !Me->IsAlive() || Me->IsInLoadout() || !HasAmmoInClip())
	{
		return false;
	}
	if (!HasAuthority())
	{
		SetState(EMFWeaponState::Firing, FireTime);   // the server decides who gets healed
		return true;
	}
	if (!bAlt)
	{
		// PulseHeal: everyone friendly in view within the radius who is hurt
		int32 Healed = 0;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MFTotemPulse), false, Me);
		for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
		{
			AMFCharacter* C = *It;
			if (!IsFriendly(C) || !C->IsAlive() || C->GetHealth() >= C->MaxHealth
				|| FVector::Dist(C->GetActorLocation(), Me->GetActorLocation()) > PulseRadius)
			{
				continue;
			}
			FHitResult Hit;
			if (C != Me && GetWorld()->LineTraceSingleByChannel(Hit, Me->GetActorLocation(), C->GetActorLocation(), ECC_Visibility, Params) && Hit.GetActor() != C)
			{
				continue;   // VisibleCollidingActors
			}
			C->Heal(PulseHeal);
			++Healed;
		}
		if (Healed == 0)
		{
			Message(TEXT("No one needs healing."));
			return false;
		}
		Message(FString::Printf(TEXT("AoE Pulse Heal activated! (%d healed)"), Healed));
		PlayAt(Me, TotemSound);
	}
	else
	{
		// TryTargetedHoT: the team-mate in the crosshair, close by
		FHitResult Hit;
		const FVector Start = GetFireStart();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MFTotemTarget), false, Me);
		AMFCharacter* Target = nullptr;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + GetAimRotation().Vector() * TargetRange, ECC_GameTraceChannel2, Params))
		{
			Target = Cast<AMFCharacter>(Hit.GetActor());
		}
		if (!Target || Target == Me || !IsFriendly(Target) || !Target->IsAlive())
		{
			Message(TEXT("Target not found."));
			return false;
		}
		if (Target->GetHealth() >= Target->MaxHealth)
		{
			Message(TEXT("Target is already at full health."));
			return false;
		}
		UMFTotemHealComponent::Apply(Target, AllyHealPerTick, HealTicks, HealInterval, 0.f);
		Message(TEXT("Injected ally with Sustained Regeneration!"));
	}
	SetState(EMFWeaponState::Firing, FireTime);
	UseCharge();
	return true;
}

void AMFWeapon_Totem::StartReload()
{
	// Reload: the sustained heal on yourself (no ammo to reload)
	if (GetLocalRole() < ROLE_Authority)
	{
		Super::StartReload();   // sends ServerReload
		return;
	}
	AMFCharacter* Me = GetOwnerCharacter();
	if (!Me || !Me->IsAlive() || Me->GetCurrentWeapon() != this || State != EMFWeaponState::Idle || !HasAmmoInClip())
	{
		return;
	}
	if (Me->GetHealth() >= Me->MaxHealth)
	{
		Message(TEXT("Already at full health."));
		return;
	}
	UMFTotemHealComponent::Apply(Me, SelfHealPerTick, HealTicks, HealInterval, 0.f);
	Message(TEXT("Sustained Regeneration activated."));
	SetState(EMFWeaponState::Firing, FireTime);
	UseCharge();
}

#undef LOCTEXT_NAMESPACE
