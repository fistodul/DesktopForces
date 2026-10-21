#pragma once

#include "CoreMinimal.h"
#include "Engine/Console.h"
#include "MFConsole.generated.h"

/**
 * MF's console keys: the console key opens (and closes) the full console with the log at once;
 * Tab (MF "Type", AMFPlayerController::Type) opens the one-line console. UE's own console
 * goes line -> full -> closed on the console key.
 */
UCLASS()
class DESKTOPFORCES_API UMFConsole : public UConsole
{
	GENERATED_BODY()

public:
	virtual bool InputKey(FInputDeviceId DeviceId, FKey Key, EInputEvent Event, float AmountDepressed = 1.f, bool bGamepad = false) override;
};
