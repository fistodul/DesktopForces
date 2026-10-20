#include "Game/MFMessages.h"
#include "Core/MFTypes.h"
#include "Game/MFGameState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "UI/MFHUD.h"

#define LOCTEXT_NAMESPACE "MFMessages"

void UMFDeathMessage::ClientReceive(const FClientReceiveData& ClientData) const
{
	AMFHUD* HUD = ClientData.LocalPC ? ClientData.LocalPC->GetHUD<AMFHUD>() : nullptr;
	if (!HUD)
	{
		return;
	}
	const UClass* TypeClass = Cast<UClass>(ClientData.OptionalObject);
	const UMFDamageType* Type = TypeClass ? Cast<UMFDamageType>(TypeClass->GetDefaultObject()) : nullptr;
	const FString Killer = ClientData.RelatedPlayerState_1 ? ClientData.RelatedPlayerState_1->GetPlayerName() : FString();
	const FString Victim = ClientData.RelatedPlayerState_2 ? ClientData.RelatedPlayerState_2->GetPlayerName() : TEXT("Someone");
	const bool bSuicide = Killer.IsEmpty() || ClientData.RelatedPlayerState_1 == ClientData.RelatedPlayerState_2;
	HUD->AddKillMessage(bSuicide ? FString() : Killer, Victim, bSuicide ? TEXT("died") : (Type ? Type->DeathVerb.ToString() : TEXT("killed")));
}

FText UMFAnnouncement::GetText(int32 Switch, const APlayerState* Subject, const AMFGameState* GS)
{
	const FText Name = FText::FromString(Subject ? Subject->GetPlayerName() : FString());
	if (Switch == FirstBlood)
	{
		return FText::Format(LOCTEXT("FirstBlood", "{0} drew first blood!"), Name);
	}
	if (Switch >= Spree && Switch < Spree + 6)
	{
		// RPlayer.spreenote
		static const FText Notes[] = {
			LOCTEXT("Spree0", "is on a killing spree!"), LOCTEXT("Spree1", "is on a rampage!"),
			LOCTEXT("Spree2", "is dominating!"), LOCTEXT("Spree3", "is brutalizing the competition!"),
			LOCTEXT("Spree4", "is unstoppable!"), LOCTEXT("Spree5", "owns you!") };
		return FText::Format(LOCTEXT("SpreeFmt", "{0} {1}"), Name, Notes[Switch - Spree]);
	}
	if (Switch == PointCaptured)
	{
		return FText::Format(LOCTEXT("Captured", "{0} captured the point"), Name);
	}
	if (Switch == MatchEnd && GS)
	{
		return FText::FromString(GS->WinnerText);
	}
	return FText::GetEmpty();
}

void UMFAnnouncement::ClientReceive(const FClientReceiveData& ClientData) const
{
	if (AMFHUD* HUD = ClientData.LocalPC ? ClientData.LocalPC->GetHUD<AMFHUD>() : nullptr)
	{
		const AMFGameState* GS = ClientData.LocalPC->GetWorld()->GetGameState<AMFGameState>();
		HUD->Announce(GetText(ClientData.MessageIndex, ClientData.RelatedPlayerState_1, GS).ToString());
	}
}

#undef LOCTEXT_NAMESPACE
