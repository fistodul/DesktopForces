#pragma once

#include "CoreMinimal.h"
#include "Game/DFGameMode.h"
#include "TrailerGame.generated.h"

class ADFVehicle;

/**
 * Trailer (MF TrailerGame): one neutral cab with a bomb trailer starts at the map's
 * TrailerGenerator. Whoever drives it tries to get it into the enemy's delivery bay
 * (TrailerDelivery, radius 128 + 128 uu around it); there it locks, counts down 5 s and
 * blows up (600, radius 1200 uu): a team point, +7 for the driver. Then a new one comes 5 s
 * later, also after the cab or trailer gets destroyed on the way. 4 deliveries / 10 min,
 * respawn 5 s. The map's other vehicle generators stay on, except at the bays.
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API ATrailerGame : public ADFGameMode
{
	GENERATED_BODY()

public:
	/** trailerGameV2 (MF-mods): the cab starts at a random road point within this distance of
	 *  the map's trailer generator, pointing along the road either way; 0 = always on the
	 *  generator (the stock game, and what the servers ran). cm. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "DF|Trailer")
	float TrailerScatterRadius = 0.f;

	ATrailerGame();
	virtual void HandleMatchHasStarted() override;
	virtual void VehicleEntered(ADFVehicle* Vehicle, ADFCharacter* Driver) override;
	virtual void GetVehicleExclusions(TArray<FVector>& OutPoints) const override;
	virtual void GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const override;
	virtual FString GetOrderString(int32 Order) const override;
	virtual FName GetOrderSound(int32 Order) const override;

	/** Delivery bays: centre and team (HUD minimap). */
	struct FBay { FVector Location; uint8 Team; float Radius; float Height; };

protected:
	virtual void CheckEndConditions(ADFPlayerState* LastKiller) override {}
	void FindMapData();
	void SpawnTrailer();
	void CheckDelivery();
	void CountDown();
	void Detonate();

	UPROPERTY(Transient)
	TObjectPtr<ADFVehicle> Cab;
	UPROPERTY(Transient)
	TObjectPtr<ADFVehicle> TrailerVehicle;

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
