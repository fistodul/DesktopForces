#include "Game/DFPlayerStart.h"
#include "Components/CapsuleComponent.h"

ADFPlayerStart::ADFPlayerStart(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GetCapsuleComponent()->InitCapsuleSize(38.25f, 87.75f);
}
