#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Core/MFTypes.h"
#include "MFPlayerState.generated.h"

/** MF PlayerReplicationInfo: team, frags (APlayerState::Score), deaths, killing spree. */
UCLASS()
class MOBILEFORCES_API AMFPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void CopyProperties(APlayerState* PlayerState) override;

	UFUNCTION(BlueprintPure, Category = "MF")
	EMFTeam GetTeam() const { return Team; }
	void SetTeam(EMFTeam NewTeam) { Team = NewTeam; }

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	int32 Kills = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MF")
	int32 Deaths = 0;

	/** Kills since last death (MF Pawn.Spree; announcements from 5). */
	int32 Spree = 0;

	/** Server time of the last death (respawn countdown). */
	UPROPERTY(Replicated)
	float DeathTime = -1000.f;

protected:
	UPROPERTY(Replicated)
	EMFTeam Team = EMFTeam::None;
};
