#pragma once

#include "CoreMinimal.h"
#include "Game/MFGameMode.h"
#include "MFGameMode_Tug.generated.h"

/**
 * Tug of War (MF-mods TugGame): two teams; an enemy you kill doesn't die, they switch to
 * your team on the spot with full health. A team that runs out of players loses the round
 * (the last converter +2). A round lasts TimeLimit minutes; when it runs out the bigger team
 * wins it. First to GoalTeamScore rounds (3) wins; every round starts with the original teams.
 */
UCLASS(Config = Game)
class MFMODTUG_API AMFGameMode_Tug : public AMFGameMode
{
	GENERATED_BODY()

public:
	AMFGameMode_Tug();
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual bool PreventDeath(AMFCharacter* Victim, AController* Killer, TSubclassOf<UDamageType> DamageType) override;

	/** Convert on the kill (MF bKillTransform); off = killed players respawn as usual, on their own team. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Tug")
	bool bKillTransform = true;

protected:
	virtual void CheckEndConditions(AMFPlayerState* LastKiller) override {}
	virtual void OnTimeLimit() override;
	/** Winner < 0: the bigger team (coin toss on a tie). */
	void RoundEnded(int32 Winner);
	void NextRound();
	int32 TeamSize(uint8 Team) const;

	TMap<TWeakObjectPtr<AController>, uint8> InitialTeams;
	FTimerHandle RoundTimer;
	bool bRoundOver = false;
};
