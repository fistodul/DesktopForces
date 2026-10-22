#include "Game/DFMessages.h"
#include "Core/DFTypes.h"
#include "Game/DFGameState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "UI/DFHUD.h"
#include "Game/DFPlayerState.h"
#include "Player/DFPlayerController.h"

#define LOCTEXT_NAMESPACE "DFMessages"

void UDFDeathMessage::ClientReceive(const FClientReceiveData& ClientData) const
{
	ADFHUD* HUD = ClientData.LocalPC ? ClientData.LocalPC->GetHUD<ADFHUD>() : nullptr;
	if (!HUD)
	{
		return;
	}
	const UClass* TypeClass = Cast<UClass>(ClientData.OptionalObject);
	const UDFDamageType* Type = TypeClass ? Cast<UDFDamageType>(TypeClass->GetDefaultObject()) : nullptr;
	const FString Killer = ClientData.RelatedPlayerState_1 ? ClientData.RelatedPlayerState_1->GetPlayerName() : FString();
	const FString Victim = ClientData.RelatedPlayerState_2 ? ClientData.RelatedPlayerState_2->GetPlayerName() : TEXT("Someone");
	const bool bSuicide = Killer.IsEmpty() || ClientData.RelatedPlayerState_1 == ClientData.RelatedPlayerState_2;
	HUD->AddKillMessage(bSuicide ? FString() : Killer, Victim, bSuicide ? TEXT("died") : (Type ? Type->DeathVerb.ToString() : TEXT("killed")));
}

FText UDFAnnouncement::GetText(int32 Switch, const APlayerState* Subject, const ADFGameState* GS, const APlayerState* Other)
{
	const FText Name = FText::FromString(Subject ? Subject->GetPlayerName() : FString());
	if (Switch == FirstBlood)
	{
		return FText::Format(LOCTEXT("FirstBlood", "{0} drew first blood!"), Name);
	}
	if (Switch >= Spree && Switch < Spree + 5)
	{
		// RageMessageKillingSpree.SpreeNote
		static const FText Notes[] = {
			LOCTEXT("Spree0", "is a one man killing machine!"), LOCTEXT("Spree1", "is seriously deranged!"),
			LOCTEXT("Spree2", "has gone ballistic!"), LOCTEXT("Spree3", "is gunning for blood!"),
			LOCTEXT("Spree4", "is a MOBILE WAR HERO!!!") };
		return FText::Format(LOCTEXT("SpreeFmt", "{0} {1}"), Name, Notes[Switch - Spree]);
	}
	if (Switch == SpreeEnded)
	{
		const FText Victim = FText::FromString(Other ? Other->GetPlayerName() : FString());
		return Subject ? FText::Format(LOCTEXT("SpreeEnded", "{0}'s killing spree was ended by {1}."), Victim, Name)
			: FText::Format(LOCTEXT("SpreeSelf", "{0} was looking good till they killed themselves!"), Victim);
	}
	if (Switch == Headshot)
	{
		return LOCTEXT("Headshot", "Decapitation!");
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

void UDFAnnouncement::ClientReceive(const FClientReceiveData& ClientData) const
{
	APlayerController* PC = ClientData.LocalPC;
	ADFHUD* HUD = PC ? PC->GetHUD<ADFHUD>() : nullptr;
	if (!HUD)
	{
		return;
	}
	const int32 Switch = ClientData.MessageIndex;
	const ADFGameState* GS = PC->GetWorld()->GetGameState<ADFGameState>();
	HUD->Announce(GetText(Switch, ClientData.RelatedPlayerState_1, GS, ClientData.RelatedPlayerState_2).ToString());
	// the announcer: first kill and sprees only to whoever made them (RageMessageFirstBlood,
	// RageMessageKillingSpree), "Blue / Red switches" to everyone (RageDomMessage)
	ADFPlayerController* MPC = Cast<ADFPlayerController>(PC);
	const bool bMine = ClientData.RelatedPlayerState_1 && ClientData.RelatedPlayerState_1 == PC->PlayerState;
	static const TCHAR* SpreeLines[] = { TEXT("killing_machine"), TEXT("seriously_deranged"), TEXT("ballistic"), TEXT("gunning_for_blood"), TEXT("MOBILE_WAR_HERO") };
	if (!MPC)
	{
		return;
	}
	if (Switch == FirstBlood && bMine)
	{
		MPC->PlayAnnouncer(TEXT("first_kill"));
	}
	else if (Switch >= Spree && Switch < Spree + 5 && bMine)
	{
		MPC->PlayAnnouncer(SpreeLines[Switch - Spree]);
	}
	else if (Switch == Headshot)
	{
		MPC->PlayAnnouncer(TEXT("Decapitation"));
	}
	else if (Switch == PointCaptured)
	{
		const ADFPlayerState* PS = Cast<ADFPlayerState>(ClientData.RelatedPlayerState_1);
		MPC->PlayAnnouncer(PS && PS->GetTeam() == 1 ? TEXT("Red_switches") : TEXT("Blue_switches"));
	}
}

#undef LOCTEXT_NAMESPACE
