#pragma once

#include "CoreMinimal.h"
#include "Game/MFGameMode.h"
#include "MFCaptains.generated.h"

class AMFPlayerState;

/**
 * Captains (MF RageCaptains): each team has a random captain (AMFPlayerState::bCaptain).
 * Killing the enemy captain is a team point (+7 for the killer); a new captain is picked a
 * few seconds later (PauseBetweenCaptains). A captain who dies by their own hand is replaced
 * at once, no point. Captains respawn at their team's CaptainStart and are sent back there
 * from loadout zones and the corridors next to the start rooms (CaptainProhibitedZone).
 * 5 points.
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API AMFGameMode_Captains : public AMFGameMode
{
	GENERATED_BODY()

public:
	AMFGameMode_Captains();
	virtual void HandleMatchHasStarted() override;
	virtual void Logout(AController* Exiting) override;
	virtual void Killed(AController* Killer, AController* Victim, AMFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void GetBotGoals(AController* Bot, TArray<AActor*>& OutGoals) const override;
	virtual FString GetOrderString(int32 Order) const override;
	virtual FName GetOrderSound(int32 Order) const override;

	AMFPlayerState* GetCaptain(uint8 Team) const;

	/** Seconds without a captain after one is killed (MF PauseBetweenCaptains 3, counted in 1 s ticks). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "MF")
	int32 PauseBetweenCaptains = 3;

protected:
	virtual void CheckEndConditions(AMFPlayerState* LastKiller) override {}
	void SetCaptain(uint8 Team, AMFPlayerState* NewCaptain);
	void ChooseRandomCaptain(uint8 Team);
	/** 1 Hz: captain picks after the pause, captains out of forbidden places. */
	void CaptainTick();
	AActor* FindCaptainStart(uint8 Team) const;

	int32 WaitForCaptain[2] = { -1, -1 };
	FTimerHandle CaptainTimer;
};
