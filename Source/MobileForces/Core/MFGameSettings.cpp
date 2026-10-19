#include "Core/MFGameSettings.h"
#include "Weapons/MFWeapon.h"
#include "UObject/UObjectIterator.h"

UMFGameSettings::UMFGameSettings()
{
	// Defaults live in Config/DefaultGame.ini; nothing hard-coded here on purpose.
}

FText UMFGameSettings::GetTeamName(uint8 TeamId) const
{
	return Teams.IsValidIndex(TeamId) ? Teams[TeamId].Name : FText::GetEmpty();
}

FLinearColor UMFGameSettings::GetTeamColor(uint8 TeamId) const
{
	return Teams.IsValidIndex(TeamId) ? Teams[TeamId].Color : FLinearColor::White;
}

const TArray<TSubclassOf<AMFWeapon>>& UMFGameSettings::GetLoadoutItems() const
{
	if (CachedItems.Num() == 0)
	{
		for (const TSoftClassPtr<AMFWeapon>& Soft : LoadoutItems)
		{
			if (UClass* C = Soft.LoadSynchronous())
			{
				CachedItems.Add(C);
			}
		}
		// Weapons from mod plugins flag themselves (bAddToLoadout) instead of editing config
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->IsChildOf(AMFWeapon::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
				&& It->GetDefaultObject<AMFWeapon>()->bAddToLoadout && !CachedItems.Contains(*It))
			{
				CachedItems.Add(*It);
			}
		}
	}
	return CachedItems;
}
