#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "GenericTeamAgentInterface.h"
#include "Core/DFTypes.h"
#include "DFPlayerState.generated.h"

/**
 * Per-player replicated state: team, frags (APlayerState::Score), deaths, spree.
 * The team lives here (not on the pawn) so it survives respawns; pawns and bot
 * controllers forward IGenericTeamAgentInterface to it.
 */
UCLASS()
class DESKTOPFORCES_API ADFPlayerState : public APlayerState, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void CopyProperties(APlayerState* PlayerState) override;

	// IGenericTeamAgentInterface
	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(Team); }
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override { Team = NewTeamId.GetId(); }

	UFUNCTION(BlueprintPure, Category = "DF")
	uint8 GetTeam() const { return Team; }
	void SetTeam(uint8 NewTeam) { Team = NewTeam; }
	bool HasTeam() const { return Team != DF::NoTeam; }

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	int32 Kills = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	int32 Deaths = 0;

	/** Kills since last death (announcements every 5). */
	int32 Spree = 0;

	/** Logged in as admin (AdminLogin; the listen server's host and single player are). */
	UPROPERTY(ReplicatedUsing = OnRep_Admin, BlueprintReadOnly, Category = "DF")
	bool bAdmin = false;
	UFUNCTION()
	void OnRep_Admin();

	/** Team captain (MF Pawn.bCaptain, Captains mode). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "DF")
	bool bCaptain = false;

	/** Server time of the last death (respawn countdown). */
	UPROPERTY(Replicated)
	float DeathTime = -1000.f;

protected:
	UPROPERTY(Replicated)
	uint8 Team = DF::NoTeam;
};
