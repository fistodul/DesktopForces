#pragma once

#include "CoreMinimal.h"
#include "GameFramework/LocalMessage.h"
#include "DFMessages.generated.h"

/**
 * Server -> client messages through UE's localized message system
 * (AGameMode::BroadcastLocalized -> APlayerController::ClientReceiveLocalizedMessage),
 * the direct descendant of MF's LocalMessage classes (RageMessageDeath, RageMessageKillingSpree ...).
 * Clients hand them to ADFHUD for display.
 */

/** Kill feed. PS1 = killer (may be null), PS2 = victim, OptionalObject = damage type class. */
UCLASS()
class DESKTOPFORCES_API UDFDeathMessage : public ULocalMessage
{
	GENERATED_BODY()
public:
	virtual void ClientReceive(const FClientReceiveData& ClientData) const override;
};

/** Centre-screen announcements. Switch selects the text; PS1 is the subject. */
UCLASS()
class DESKTOPFORCES_API UDFAnnouncement : public ULocalMessage
{
	GENERATED_BODY()
public:
	enum : int32
	{
		FirstBlood = 0,
		Spree = 1,          // + spree level 0..4: 5, 10, 15, 20, 25 kills (RageMessageKillingSpree)
		SpreeEnded = 6,     // PS1 = the killer (null: suicide), PS2 = who was on the spree
		Headshot = 7,       // to the killer only (RageMessageDecapitation)
		PointCaptured = 10, // OptionalObject = AHoldoutPoint
		MatchEnd = 20,      // text comes from ADFGameState::WinnerText
	};
	virtual void ClientReceive(const FClientReceiveData& ClientData) const override;
	static FText GetText(int32 Switch, const class APlayerState* Subject, const class ADFGameState* GS, const class APlayerState* Other = nullptr);
};
