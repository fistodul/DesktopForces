#pragma once

#include "CoreMinimal.h"
#include "GameFramework/LocalMessage.h"
#include "MFMessages.generated.h"

/**
 * Server -> client messages through UE's localized message system
 * (AGameMode::BroadcastLocalized -> APlayerController::ClientReceiveLocalizedMessage),
 * the direct descendant of MF's LocalMessage classes (RageMessageDeath, RageMessageKillingSpree ...).
 * Clients hand them to AMFHUD for display.
 */

/** Kill feed. PS1 = killer (may be null), PS2 = victim, OptionalObject = damage type class. */
UCLASS()
class DESKTOPFORCES_API UMFDeathMessage : public ULocalMessage
{
	GENERATED_BODY()
public:
	virtual void ClientReceive(const FClientReceiveData& ClientData) const override;
};

/** Centre-screen announcements. Switch selects the text; PS1 is the subject. */
UCLASS()
class DESKTOPFORCES_API UMFAnnouncement : public ULocalMessage
{
	GENERATED_BODY()
public:
	enum : int32
	{
		FirstBlood = 0,
		Spree = 1,       // + spree level 0..5 (MF RPlayer.spreenote)
		PointCaptured = 10, // OptionalObject = AMFHoldoutPoint
		MatchEnd = 20,   // text comes from AMFGameState::WinnerText
	};
	virtual void ClientReceive(const FClientReceiveData& ClientData) const override;
	static FText GetText(int32 Switch, const class APlayerState* Subject, const class AMFGameState* GS);
};
