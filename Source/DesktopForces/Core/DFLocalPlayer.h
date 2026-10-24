// The local player: logs in under the name from Options.
#pragma once

#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"
#include "DFLocalPlayer.generated.h"

UCLASS()
class DESKTOPFORCES_API UDFLocalPlayer : public ULocalPlayer {
	GENERATED_BODY()

public:
	/** The login's Name= (every map, every server joined): Options' player name instead of the
	 *  platform's nickname (without an online service: the computer's name). */
	virtual FString GetNickname() const override;
};
