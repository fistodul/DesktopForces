#pragma once

#include "CoreMinimal.h"
#include "Weapons/DFWeapon.h"
#include "TripBombs.generated.h"

/**
 * Trip bombs: fire plants a bomb on the surface in front (within 50 uu); after 1 s its
 * laser spans to the opposite surface (max 1024 uu) and anything crossing it sets it off
 * (200 damage, radius 400). Alt winds up a throw (power 3-10, +5/s) and drops a proximity
 * mine (120 damage, radius 200, triggers within 64 uu). Two per item, max 3 items.
 */
UCLASS()
class DESKTOPFORCES_API ATripBombs : public ADFWeapon {
	GENERATED_BODY()
public:
	ATripBombs();
	virtual void StartFire(bool bAlt) override;
	virtual void StopFire(bool bAlt) override;
	virtual int32 GetPowerLevel() const override;

	virtual void ForceInactive() override;

protected:
	// NormalFire: PlaceForward, then the plant (or not) and PlaceBack / PlaceBombDown
	UFUNCTION(Server, Reliable)
	void ServerPlace();
	void BeginPlace();
	void EndPlace();
	// AltFiring: SlideBack, charge while held, SlideForward and the throw on release
	UFUNCTION(Server, Reliable)
	void ServerThrow(float Power);
	void BeginWindUp();
	void WindUpReady();
	void Throw();
	bool ThrowNow(float Power);
	void Done();
	void BusyFor(float Seconds, void (ATripBombs::*Then)());
	void PlayArms(FName Name, float Duration);
	/** A wall in reach (and, for the bomb's beam, a surface across): ClientTestForStick. */
	bool CanPlant() const;
	bool TryPlant();
	/** Throw speed for a wind-up of Power (3-10): MF ThrowPower * 125 uu/s. */
	virtual float GetThrowSpeed(float Power) const { return Power * 281.25f; }
	/** Server, before a plant or throw: may this owner put down another one? (mods: limits) */
	virtual bool CanPlace() const { return true; }
	/** What gets planted on walls. */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	TSubclassOf<class ATripBomb> BombClass;
	/** Planting needs something within MaxLaserDistance for the beam (MF TripBombs; TripLasers don't). */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	bool bNeedLaserTarget = true;
	// the arms' sequences at MF's rates (TripBombsPlayerMesh)
	float PlaceTime = 1.037f; // PlaceForward 0.93 s at 0.9
	float PlaceBackTime = 0.444f; // PlaceBack / PlaceBombDown 0.4 s at 0.9
	float SlideBackTime = 0.367f; // SlideBack 12 @30
	float SlideForwardTime = 0.6f; // SlideForward 19 @30
	double WindUpStart = 0.0; // when the charge starts (SlideBack's end)
	bool bWinding = false;
	bool bReleased = false;
	FTimerHandle StepTimer;
	float GetThrowPower() const;
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float MaxLaserDistance = 2304.f; // MF 1024
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float PlaceReach = 112.5f; // MF trace 50 uu
};
