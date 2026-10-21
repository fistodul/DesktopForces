#pragma once

#include "CoreMinimal.h"
#include "NavigationSystem.h"
#include "MFNavigationSystem.generated.h"

/**
 * The navigation system (DefaultEngine.ini [/Script/Engine.Engine] NavigationSystemClassName;
 * the importer writes it into each map's world settings). Maps ship a static navmesh baked
 * with every map actor. Two things made UE print "NAVMESH NEEDS TO BE REBUILT" all game long
 * in development builds:
 *  - the game type takes some actors away when a match starts (GameInfo.IsRelevant: team-only
 *    doors, other modes' movers); a static navmesh can't follow, the mode forgets it;
 *  - UE 5.8 loads the baked tiles but keeps the navmesh flagged as "no tile data".
 * The warning here is for real changes after the start (something that should be baked).
 */
UCLASS()
class DESKTOPFORCES_API UMFNavigationSystem : public UNavigationSystemV1
{
	GENERATED_BODY()

public:
	UMFNavigationSystem(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Forget the changes made while setting the map up for the game type. */
	void ForgetStartupChanges() { ResetDefaultDirtyAreasController(); }

#if !UE_BUILD_SHIPPING
private:
	void GetMFOnScreenMessages(TMultiMap<FCoreDelegates::EOnScreenMessageSeverity, FText>& OutMessages);
#endif
};
