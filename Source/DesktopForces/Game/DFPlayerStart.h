#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "Core/DFTypes.h"
#include "DFPlayerStart.generated.h"

/**
 * MF PlayerStart. TeamNumber: 0 blue, 1 red, 255 (anything >= 2) neutral.
 * Non-team games only use neutral starts; team games use their team's starts
 * (RageDeathMatch.FindPlayerStart, map-content 10G).
 */
UCLASS()
class DESKTOPFORCES_API ADFPlayerStart : public APlayerStart {
	GENERATED_BODY()

public:
	ADFPlayerStart(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DF")
	uint8 TeamNumber = 255;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DF")
	bool bEnabled = true;

	/** MF CaptainStart: only the team's captain spawns here (Captains); unused elsewhere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DF")
	bool bCaptainStart = false;

	bool IsNeutral() const { return TeamNumber >= 2; }
};
