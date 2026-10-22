#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "DFGameInstance.generated.h"

/** Game-wide state that survives map changes (settings, future menus / server browser). */
UCLASS()
class DESKTOPFORCES_API UDFGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void OnStart() override;

private:
	/** -DFAutoShot / -DFQuitAfter / -DFExec smoke-test hooks (see .cpp). */
	void ScheduleAutoTest();

	FString AutoShotName;
	TArray<FString> AutoExec;
};
