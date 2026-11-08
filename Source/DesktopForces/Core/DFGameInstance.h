#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Containers/Ticker.h"
#include "DFGameInstance.generated.h"

/** Game-wide state that survives map changes (settings, future menus / server browser). */
UCLASS()
class DESKTOPFORCES_API UDFGameInstance : public UGameInstance {
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void OnStart() override;
	virtual void Shutdown() override;

private:
	/**
	 * Phones (or -DFStepDown): while playing, when the frame rate stays under 90 % of the cap
	 * for 30 s (thermal throttling), lower the cap a step (UDFUserSettings::RecommendedFrameRates)
	 * for this session; the saved setting stays.
	 */
	bool StepDownTick(float DeltaTime);
	FTSTicker::FDelegateHandle StepDownTicker;
	float StepDownCap = 0.f; // 0: the saved cap
	float WindowTime = 0.f;
	int32 WindowFrames = 0;
	int32 SlowWindows = 0;

	/** -DFAutoShot / -DFQuitAfter / -DFExec smoke-test hooks (see .cpp). */
	void ScheduleAutoTest();

	FString AutoShotName;
	TArray<FString> AutoExec;
};
