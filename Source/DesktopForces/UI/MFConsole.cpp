#include "UI/MFConsole.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/InputSettings.h"

bool UMFConsole::InputKey(FInputDeviceId DeviceId, FKey Key, EInputEvent Event, float AmountDepressed, bool bGamepad)
{
	const FModifierKeysState Mods = FSlateApplication::Get().GetModifierKeys();
	if (Event == IE_Pressed && !Mods.AnyModifiersDown() && GetDefault<UInputSettings>()->ConsoleKeys.Contains(Key))
	{
		FakeGotoState(ConsoleState == NAME_None ? FName(TEXT("Open")) : FName(NAME_None));
		bCaptureKeyInput = true;   // no console key character in the input line
		return true;
	}
	return Super::InputKey(DeviceId, Key, Event, AmountDepressed, bGamepad);
}
