#include "Core/DFLocalPlayer.h"
#include "Core/DFUserSettings.h"

FString UDFLocalPlayer::GetNickname() const {
	const FString& Name = UDFUserSettings::Get().PlayerName;
	return Name.IsEmpty() ? Super::GetNickname() : Name.Replace(TEXT("?"), TEXT("")).Left(20);
}
