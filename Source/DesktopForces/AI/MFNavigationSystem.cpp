#include "AI/MFNavigationSystem.h"

#define LOCTEXT_NAMESPACE "MFNavigation"

UMFNavigationSystem::UMFNavigationSystem(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if !UE_BUILD_SHIPPING
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		FCoreDelegates::OnGetOnScreenMessages.RemoveAll(this);   // UNavigationSystemV1's own
		FCoreDelegates::OnGetOnScreenMessages.AddUObject(this, &UMFNavigationSystem::GetMFOnScreenMessages);
	}
#endif
}

#if !UE_BUILD_SHIPPING
void UMFNavigationSystem::GetMFOnScreenMessages(TMultiMap<FCoreDelegates::EOnScreenMessageSeverity, FText>& OutMessages)
{
	if (DefaultDirtyAreasController.HadDirtyAreasReportedWhileAccumulationLocked())
	{
		OutMessages.Add(FCoreDelegates::EOnScreenMessageSeverity::Error, LOCTEXT("NavChanged", "NAVMESH NEEDS TO BE REBUILT (changed in play)"));
	}
}
#endif

#undef LOCTEXT_NAMESPACE
