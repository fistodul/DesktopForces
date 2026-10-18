#include "Game/MFPlayerStart.h"
#include "Components/CapsuleComponent.h"

AMFPlayerStart::AMFPlayerStart(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GetCapsuleComponent()->InitCapsuleSize(38.25f, 87.75f);
}
