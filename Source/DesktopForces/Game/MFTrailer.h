#pragma once

#include "CoreMinimal.h"
#include "Game/MFGameMode.h"
#include "MFTrailer.generated.h"

class AMFVehicle;

/**
 * Trailer (MF TrailerGame): one neutral cab with a bomb trailer starts at the map's
 * TrailerGenerator. Whoever drives it tries to get it into the enemy's delivery bay
 * (TrailerDelivery, radius 128 + 128 uu around it); there it locks, counts down 5 s and
 * blows up (600, radius 1200 uu): a team point, +7 for the driver. Then a new one comes 5 s
 * later, also after the cab or trailer gets destroyed on the way. 4 deliveries / 10 min,
 * respawn 5 s. The map's other vehicle generators stay on, except at the bays.
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API AMFGameMode_Trailer : public AMFGameMode
{
	GENERATED_BODY()

public:
	AMFGameMode_Trailer();
	virtual void HandleMatchHasStarted() override;
	virtual void VehicleEntered(AMFVehicle* Vehicle, AMFCharacter* Driver) override;
	virtual void GetVehicleExclusions(TArray<FVector>& OutPoints) const override;
	virtual void GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const override;
	virtual FString GetOrderString(int32 Order) const override;
	virtual FName GetOrderSound(int32 Order) const override;

	AMFVehicle* GetCab() const { return Cab; }
	/** Delivery bays: centre and team (HUD minimap). */
	struct FBay { FVector Location; uint8 Team; float Radius; float Height; };
	const TArray<FBay>& GetBays() const { return Bays; }

protected:
	virtual void CheckEndConditions(AMFPlayerState* LastKiller) override {}
	void FindMapData();
	void SpawnTrailer();
	void CheckDelivery();
	void CountDown();
	void Detonate();

	UPROPERTY(Transient)
	TObjectPtr<AMFVehicle> Cab;
	UPROPERTY(Transient)
	TObjectPtr<AMFVehicle> TrailerVehicle;

	TArray<FBay> Bays;
	FTransform GeneratorSpot;
	bool bHaveGenerator = false;
	int32 DeliveredTo = -1;        // bay team while the countdown runs
	int32 Count = 0;
	TWeakObjectPtr<AController> Scorer;
	FTimerHandle CheckTimer;
	FTimerHandle CountTimer;
	FTimerHandle RespawnTimer;
};
