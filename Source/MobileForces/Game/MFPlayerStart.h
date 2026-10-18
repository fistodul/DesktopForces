#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "Core/MFTypes.h"
#include "MFPlayerStart.generated.h"

/**
 * MF PlayerStart. TeamNumber: 0 blue, 1 red, 255 (anything >= 2) neutral.
 * Non-team games only use neutral starts; team games use their team's starts
 * (RageDeathMatch.FindPlayerStart, map-content 10G).
 */
UCLASS()
class MOBILEFORCES_API AMFPlayerStart : public APlayerStart
{
	GENERATED_BODY()

public:
	AMFPlayerStart(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MF")
	uint8 TeamNumber = 255;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MF")
	bool bEnabled = true;

	bool IsNeutral() const { return TeamNumber >= 2; }
};
