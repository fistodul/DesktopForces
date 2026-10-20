#pragma once

#include "CoreMinimal.h"
#include "Weapons/MFWeapon.h"
#include "MFWeapon_Armour.generated.h"

/** Body armour: passive, absorbs 75% of damage until its 100 charge is used up. */
UCLASS()
class DESKTOPFORCES_API AMFWeapon_Armour : public AMFWeapon
{
	GENERATED_BODY()
public:
	AMFWeapon_Armour();
	virtual void GiveFullAmmo() override;
	virtual int32 AbsorbDamage(int32 Damage, const UMFDamageType* Type) override;
	virtual int32 GetAbsorbPriority() const override { return 10; }
	int32 GetCharge() const { return Charge; }
protected:
	virtual bool TryFire(bool bAlt) override { return false; }
	UPROPERTY(Replicated)
	int32 Charge = 100;
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	int32 MaxCharge = 100;
	UPROPERTY(EditDefaultsOnly, Category = "MF")
	int32 ArmorAbsorption = 75;
public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
