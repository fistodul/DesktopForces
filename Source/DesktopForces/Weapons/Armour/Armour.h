#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapon.h"
#include "Armour.generated.h"

/** Body armour: passive, absorbs 75% of damage until its 100 charge is used up; reinforced
 *  (MF-mods ZombieArmour: Zombie mode's zombies, ?ReinforcedArmour) 80% of 120. */
UCLASS()
class DESKTOPFORCES_API AArmour : public ADFWeapon {
	GENERATED_BODY()
public:
	AArmour();
	virtual void GiveFullAmmo() override;
	virtual int32 AbsorbDamage(int32 Damage, const UDFDamageType* Type) override;
	virtual int32 GetAbsorbPriority() const override { return 10; }
	int32 GetCharge() const { return Charge; }
	/** Server: ZombieArmour's numbers or the stock ones; a change hands over a full one, as
	 *  MF's item swap did. */
	void SetReinforced(bool bOn);

protected:
	virtual bool TryFire(bool bAlt) override { return false; }
	UPROPERTY(Replicated)
	int32 Charge = 100;
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	int32 MaxCharge = 100;
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	int32 ArmorAbsorption = 75;

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
