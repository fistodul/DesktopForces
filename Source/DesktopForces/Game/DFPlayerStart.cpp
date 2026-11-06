#include "Game/DFPlayerStart.h"
#include "Components/CapsuleComponent.h"
#include "AI/DFBotController.h"

ADFPlayerStart::ADFPlayerStart(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer) {
	GetCapsuleComponent()->InitCapsuleSize(38.25f, 87.75f);
}

bool ADFPlayerStart::IsBotReachable() const {
	if (BotReachable < 0)
		BotReachable = ADFBotController::CanReachNavMesh(GetWorld(), GetActorLocation()) ? 1 : 0;
	return BotReachable > 0;
}
