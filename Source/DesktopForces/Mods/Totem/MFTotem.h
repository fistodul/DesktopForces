#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/MFTypes.h"
#include "Weapons/MFWeapon.h"
#include "MFTotem.generated.h"

class AMFCharacter;

/**
 * Heal over time on a character, optionally with a short grace period where no damage gets
 * through (Totem.TotemHealEffect). Added by the Totem, removes itself when done. Timer
 * driven, never ticks. Several stack.
 */
UCLASS()
class DESKTOPFORCES_API UMFTotemHealComponent : public UActorComponent, public IMFDamageFilter
{
	GENERATED_BODY()

public:
	UMFTotemHealComponent();

	/** Start healing HealPerTick every Interval for Ticks ticks; no damage for Grace seconds. */
	static UMFTotemHealComponent* Apply(AMFCharacter* Target, float HealPerTick, int32 Ticks, float Interval, float Grace);

	virtual int32 FilterDamage(int32 Damage, const UMFDamageType* Type) override;

protected:
	void HealTick();

	float HealPerTick = 0.f;
	int32 TicksLeft = 0;
	float Interval = 0.5f;
	float GraceLeft = 0.f;
	float Accumulator = 0.f;
	FTimerHandle Timer;
};

/**
 * Totem of Undying (MF-mods Totem): carried, it cheats death once (survive on 5 health,
 * 1.5 s of no damage, then a small heal over time). Fire: pulse heal of 56 for you and
 * team-mates within 900 cm in view. Alt fire: 10 s heal over time on the team-mate you
 * look at. Reload: the same on yourself. One charge; used up by any of them.
 */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_Totem : public AMFWeapon
{
	GENERATED_BODY()

public:
	AMFWeapon_Totem();
	virtual int32 AbsorbDamage(int32 Damage, const UMFDamageType* Type) override;
	virtual int32 GetAbsorbPriority() const override { return -1; }   // after armour (MF AbsorptionPriority 1)
	virtual void StartReload() override;

	UPROPERTY(EditDefaultsOnly, Category = "Totem")
	int32 SurviveHealth = 5;
	UPROPERTY(EditDefaultsOnly, Category = "Totem")
	int32 PulseHeal = 56;
	UPROPERTY(EditDefaultsOnly, Category = "Totem")
	float PulseRadius = 900.f;            // MF 400 uu
	UPROPERTY(EditDefaultsOnly, Category = "Totem")
	float SelfHealPerTick = 6.f;          // x 20 ticks, 0.5 s apart = 120 over 10 s
	UPROPERTY(EditDefaultsOnly, Category = "Totem")
	float AllyHealPerTick = 5.9f;         // 118 over 10 s
	UPROPERTY(EditDefaultsOnly, Category = "Totem")
	int32 HealTicks = 20;
	UPROPERTY(EditDefaultsOnly, Category = "Totem")
	float HealInterval = 0.5f;
	UPROPERTY(EditDefaultsOnly, Category = "Totem")
	float ReviveHealPerTick = 3.1666667f; // 19 over 3 s
	UPROPERTY(EditDefaultsOnly, Category = "Totem")
	int32 ReviveTicks = 6;
	UPROPERTY(EditDefaultsOnly, Category = "Totem")
	float ReviveGrace = 1.5f;
	UPROPERTY(EditDefaultsOnly, Category = "Totem")
	float TargetRange = 360.f;            // MF TraceRange 160 uu

protected:
	virtual bool TryFire(bool bAlt) override;
	bool IsFriendly(const AMFCharacter* Other) const;
	void Message(const FString& Text) const;
	void UseCharge();
};
