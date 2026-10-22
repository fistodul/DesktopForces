#pragma once

#include "CoreMinimal.h"
#include "Game/DFGameMode.h"
#include "TugOfWar.generated.h"

/**
 * Tug of War (MF-mods TugGame): two teams; an enemy you kill doesn't die, they switch to
 * your team on the spot with full health. A team that runs out of players loses the round
 * (the last converter +2). A round lasts TimeLimit minutes; when it runs out the bigger team
 * wins it. First to GoalTeamScore rounds (3) wins; every round starts with the original teams.
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API ATugOfWar : public ADFGameMode
{
	GENERATED_BODY()

public:
	ATugOfWar();
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual bool PreventDeath(ADFCharacter* Victim, AController* Killer, TSubclassOf<UDamageType> DamageType) override;

	/** Convert on the kill (MF bKillTransform); off = killed players respawn as usual, on their own team. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Tug")
	bool bKillTransform = true;

protected:
	virtual void CheckEndConditions(ADFPlayerState* LastKiller) override {}
	virtual void OnTimeLimit() override;
	/** Winner < 0: the bigger team (coin toss on a tie). */
	virtual void RoundEnded(int32 Winner);
	virtual void NextRound();
	int32 TeamSize(uint8 Team) const;

	TMap<TWeakObjectPtr<AController>, uint8> InitialTeams;
	FTimerHandle RoundTimer;
	bool bRoundOver = false;
};
