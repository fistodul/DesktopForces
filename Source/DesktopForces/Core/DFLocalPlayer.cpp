#include "Core/DFLocalPlayer.h"
#include "Core/DFUserSettings.h"

FString UDFLocalPlayer::GetNickname() const
{
	const UDFUserSettings* S = UDFUserSettings::Get();
	return S && !S->PlayerName.IsEmpty() ? S->PlayerName.Replace(TEXT("?"), TEXT("")).Left(20) : Super::GetNickname();
}
